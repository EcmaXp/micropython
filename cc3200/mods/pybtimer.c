/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2015 Daniel Campora
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/mpconfig.h"
#include MICROPY_HAL_H
#include "py/obj.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_timer.h"
#include "rom_map.h"
#include "interrupt.h"
#include "prcm.h"
#include "timer.h"
#include "pybtimer.h"
#include "pybsleep.h"
#include "mpcallback.h"
#include "mpexception.h"


/// \moduleref pyb
/// \class Timer - generate periodic events, count events, and create PWM signals.
///
/// Each timer consists of a counter that counts up at a certain rate.  The rate
/// at which it counts is the peripheral clock frequency (in Hz) divided by the
/// timer prescaler.  When the counter reaches the timer period it triggers an
/// event, and the counter resets back to zero.  By using the callback method,
/// the timer event can call a Python function.
///
/// Example usage to toggle an LED at a fixed frequency:
///
///     tim = pyb.Timer(4)                                              # create a timer object using timer 4
///     tim.init(mode=Timer.PERIODIC)                                   # initialize it in periodic mode
///     tim_ch = tim.channel(Timer.A, freq=2)                           # configure channel A at a frequency of 2Hz
///     tim_ch.callback(handler=lambda t:led.toggle())                  # toggle a led on every cycle of the timer
///
/// Further examples:
///
///     tim1 = pyb.Timer(2, mode=Timer.EVENT_COUNT)                     # initialize it capture mode
///     tim2 = pyb.Timer(1, mode=Timer.PWM)                             # initialize it in PWM mode
///     tim_ch = tim1.channel(Timer.A, freq=1, polarity=Timer.POSITIVE) # start the PWM on channel B with a 50% duty cycle
///     tim_ch = tim2.channel(Timer.B, freq=10000, duty_cycle=50)       # start the event counter with a frequency of 1Hz and triggered by positive edges
///     tim_ch.time()                                                   # get the current time in usec (can also be set)
///     tim_ch.freq(20)                                                 # set the frequency (can also get)
///     tim_ch.duty_cycle(30)                                           # set the duty cycle to 30% (can also get)
///     tim_ch.duty_cycle(30, Timer.NEGATIVE)                           # set the duty cycle to 30% and change the polarity to negative
///     tim_ch.event_count()                                            # get the number of captured events
///     tim_ch.event_time()                                             # get the the time of the last captured event
///

/******************************************************************************
 DECLARE PRIVATE CONSTANTS
 ******************************************************************************/
#define PYBTIMER_NUM_TIMERS                         (4)
#define PYBTIMER_POLARITY_POS                       (0x01)
#define PYBTIMER_POLARITY_NEG                       (0x02)

#define PYBTIMER_SRC_FREQ_HZ                        HAL_FCPU_HZ

/******************************************************************************
 DEFINE PRIVATE TYPES
 ******************************************************************************/
typedef struct _pyb_timer_obj_t {
    mp_obj_base_t base;
    uint32_t timer;
    uint32_t config;
    uint16_t intflags;
    uint8_t peripheral;
    uint8_t id;
} pyb_timer_obj_t;

typedef struct _pyb_timer_channel_obj_t {
    mp_obj_base_t base;
    struct _pyb_timer_obj_t *timer;
    uint32_t frequency;
    uint16_t channel;
    uint8_t  polarity;
    uint8_t  duty_cycle;
} pyb_timer_channel_obj_t;

/******************************************************************************
 DEFINE PRIVATE DATA
 ******************************************************************************/
STATIC const mp_cb_methods_t pyb_timer_channel_cb_methods;
STATIC pyb_timer_obj_t pyb_timer_obj[PYBTIMER_NUM_TIMERS] = {{.timer = TIMERA0_BASE, .peripheral = PRCM_TIMERA0},
                                                             {.timer = TIMERA1_BASE, .peripheral = PRCM_TIMERA1},
                                                             {.timer = TIMERA2_BASE, .peripheral = PRCM_TIMERA2},
                                                             {.timer = TIMERA3_BASE, .peripheral = PRCM_TIMERA3}};
STATIC const mp_obj_type_t pyb_timer_channel_type;

/******************************************************************************
 DECLARE PRIVATE FUNCTIONS
 ******************************************************************************/
STATIC mp_obj_t pyb_timer_channel_callback (mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args);
STATIC void timer_disable (pyb_timer_obj_t *tim);
STATIC void TIMER0AIntHandler(void);
STATIC void TIMER0BIntHandler(void);
STATIC void TIMER1AIntHandler(void);
STATIC void TIMER1BIntHandler(void);
STATIC void TIMER2AIntHandler(void);
STATIC void TIMER2BIntHandler(void);
STATIC void TIMER3AIntHandler(void);
STATIC void TIMER3BIntHandler(void);

/******************************************************************************
 DEFINE PUBLIC FUNCTIONS
 ******************************************************************************/
void timer_init0 (void) {
    mp_obj_list_init(&MP_STATE_PORT(pyb_timer_channel_obj_list), 0);
}

void timer_disable_all (void) {
    pyb_timer_obj_t timer = {
            .timer = TIMERA0_BASE,
            .intflags = TIMER_CAPB_EVENT   | TIMER_CAPB_MATCH |
                        TIMER_TIMB_TIMEOUT | TIMER_CAPA_EVENT |
                        TIMER_CAPA_MATCH   | TIMER_TIMA_TIMEOUT,
            .peripheral = PRCM_TIMERA0
    };

    for (uint32_t i = 0; i < PYBTIMER_NUM_TIMERS; i++) {
        // in case it's not clocked
        MAP_PRCMPeripheralClkEnable(timer.peripheral, PRCM_RUN_MODE_CLK | PRCM_SLP_MODE_CLK);
        timer_disable(&timer);
        // timer base offset according to hw_memmap.h
        timer.timer += 0x1000;
        // peripheral offset according to prcm.h
        timer.peripheral++;
    }
}

void pyb_timer_channel_callback_enable (mp_obj_t self_in) {
    pyb_timer_channel_obj_t *self = self_in;
    MAP_TimerIntClear(self->timer->timer, self->timer->intflags & self->channel);
    MAP_TimerIntEnable(self->timer->timer, self->timer->intflags & self->channel);
}

void pyb_timer_channel_callback_disable (mp_obj_t self_in) {
    pyb_timer_channel_obj_t *self = self_in;
    MAP_TimerIntDisable(self->timer->timer, self->timer->intflags & self->channel);
}

pyb_timer_channel_obj_t *pyb_timer_channel_find (uint32_t timer, uint16_t channel_n) {
    for (mp_uint_t i = 0; i < MP_STATE_PORT(pyb_timer_channel_obj_list).len; i++) {
        pyb_timer_channel_obj_t *ch = ((pyb_timer_channel_obj_t *)(MP_STATE_PORT(pyb_timer_channel_obj_list).items[i]));
        // any 32-bit timer must be matched by any of its 16-bit versions
        if (ch->timer->timer == timer && ((ch->channel & TIMER_A) == channel_n || (ch->channel & TIMER_B) == channel_n)) {
            return ch;
        }
    }
    return MP_OBJ_NULL;
}

void pyb_timer_channel_remove (pyb_timer_channel_obj_t *ch) {
    pyb_timer_channel_obj_t *channel;
    if ((channel = pyb_timer_channel_find(ch->timer->timer, ch->channel))) {
        mp_obj_list_remove(&MP_STATE_PORT(pyb_timer_channel_obj_list), channel);
    }
}

void pyb_timer_channel_add (pyb_timer_channel_obj_t *ch) {
    // remove it in case it already exists
    pyb_timer_channel_remove(ch);
    mp_obj_list_append(&MP_STATE_PORT(pyb_timer_channel_obj_list), ch);
}

STATIC void timer_disable (pyb_timer_obj_t *tim) {
    // disable all timers and it's interrupts
    MAP_TimerDisable(tim->timer, TIMER_A | TIMER_B);
    MAP_TimerIntDisable(tim->timer, tim->intflags);
    MAP_TimerIntClear(tim->timer, tim->intflags);
    MAP_PRCMPeripheralClkDisable(tim->peripheral, PRCM_RUN_MODE_CLK | PRCM_SLP_MODE_CLK);
    memset(&pyb_timer_obj[tim->id], 0, sizeof(pyb_timer_obj_t));
}

// computes prescaler period and match value so timer triggers at freq-Hz
STATIC uint32_t compute_prescaler_period_and_match_value(pyb_timer_channel_obj_t *ch, uint32_t *period_out, uint32_t *match_out) {
    uint32_t maxcount = (ch->channel == (TIMER_A | TIMER_B)) ? 0xFFFFFFFF : 0xFFFF;
    uint32_t prescaler;
    uint32_t period = PYBTIMER_SRC_FREQ_HZ / ch->frequency;

    period = MAX(1, period) - 1;
    prescaler = period >> 16;
    *period_out = period;
    if (prescaler > 0xFF && maxcount == 0xFFFF) {
        goto error;
    }
    // check limit values for the duty cycle
    if (ch->duty_cycle == 0) {
        *match_out = period - 1;
    }
    else {
        *match_out = period - ((period * ch->duty_cycle) / 100);
    }
    if ((ch->timer->config & 0x0F) == TIMER_CFG_A_PWM && (*match_out > 0xFFFF)) {
        goto error;
    }
    return prescaler;

error:
    nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, mpexception_value_invalid_arguments));
}

STATIC void timer_init (pyb_timer_obj_t *tim) {
    MAP_PRCMPeripheralClkEnable(tim->peripheral, PRCM_RUN_MODE_CLK | PRCM_SLP_MODE_CLK);
    MAP_PRCMPeripheralReset(tim->peripheral);
    MAP_TimerConfigure(tim->timer, tim->config);
}

STATIC void timer_channel_init (pyb_timer_channel_obj_t *ch) {
    // calculate the period, the prescaler and the match value
    uint32_t period;
    uint32_t match;
    uint32_t prescaler = compute_prescaler_period_and_match_value(ch, &period, &match);

    // set the prescaler
    MAP_TimerPrescaleSet(ch->timer->timer, ch->channel, (prescaler < 0xFF) ? prescaler : 0);

    // set the load value
    MAP_TimerLoadSet(ch->timer->timer, ch->channel, period);

    // configure the pwm if we are in such mode
    if ((ch->timer->config & 0x0F) == TIMER_CFG_A_PWM) {
        // invert the timer output if required
        MAP_TimerControlLevel(ch->timer->timer, ch->channel, (ch->polarity == PYBTIMER_POLARITY_NEG) ? true : false);
        // set the match value (which is simply the duty cycle translated to ticks)
        MAP_TimerMatchSet(ch->timer->timer, ch->channel, match);
    }
    // configure the event edge type if we are in such mode
    else if ((ch->timer->config & 0x0F) == TIMER_CFG_A_CAP_COUNT || (ch->timer->config & 0x0F) == TIMER_CFG_A_CAP_TIME) {
        uint32_t polarity = TIMER_EVENT_BOTH_EDGES;
        if (ch->polarity == PYBTIMER_POLARITY_POS) {
            polarity = TIMER_EVENT_POS_EDGE;
        }
        else if (ch->polarity == PYBTIMER_POLARITY_NEG) {
            polarity = TIMER_EVENT_NEG_EDGE;
        }
        MAP_TimerControlEvent(ch->timer->timer, ch->channel, polarity);
    }

#ifdef DEBUG
    // stall the timer when the processor is halted while debugging
    MAP_TimerControlStall(ch->timer->timer, ch->channel, true);
#endif

    // now enable the timer channel
    MAP_TimerEnable(ch->timer->timer, ch->channel);
}

/******************************************************************************/
/* Micro Python bindings                                                      */

STATIC void pyb_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_timer_obj_t *tim = self_in;
    uint32_t mode = tim->config & 0xFF;

    // timer mode
    qstr mode_qst = MP_QSTR_PWM;
    switch(mode) {
    case TIMER_CFG_A_ONE_SHOT:
        mode_qst = MP_QSTR_ONE_SHOT;
        break;
    case TIMER_CFG_A_PERIODIC:
        mode_qst = MP_QSTR_PERIODIC;
        break;
    case TIMER_CFG_A_CAP_COUNT:
        mode_qst = MP_QSTR_EDGE_COUNT;
        break;
    case TIMER_CFG_A_CAP_TIME:
        mode_qst = MP_QSTR_EDGE_TIME;
        break;
    default:
        break;
    }
    mp_printf(print, "<Timer%u, mode=Timer.%q>", (tim->id + 1), mode_qst);
}

/// \method init(mode, *, width)
/// Initialise the timer.  Initialisation must give the desired mode
/// and an optional timer width
///
///     tim.init(mode=Timer.PERIODIC)                  # configure in free running periodic mode
///     tim.init(mode=Timer.ONE_SHOT, width=16)        # one shot mode splitted into two 16-bit independent timers
///
/// Keyword arguments:
///
///   - `width` - specifies the width of the timer. Default is 32 bit mode. When in 16 bit mode
///               the timer is splitted into 2 independent channels.
///
STATIC mp_obj_t pyb_timer_init_helper(pyb_timer_obj_t *tim, mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode,         MP_ARG_REQUIRED | MP_ARG_INT, },
        { MP_QSTR_width,        MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 32} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // check the mode
    uint32_t _mode = args[0].u_int;
    if (_mode != TIMER_CFG_A_ONE_SHOT && _mode != TIMER_CFG_A_PERIODIC && _mode != TIMER_CFG_A_CAP_COUNT &&
        _mode != TIMER_CFG_A_CAP_TIME && _mode != TIMER_CFG_A_PWM) {
        goto error;
    }

    // check the width
    if (args[1].u_int != 16 && args[1].u_int != 32) {
        goto error;
    }
    bool is16bit = (args[1].u_int == 16);

    if (!is16bit && (_mode != TIMER_CFG_A_ONE_SHOT && _mode != TIMER_CFG_A_PERIODIC)) {
        // 32-bit mode is only available when in free running modes
        goto error;
    }
    tim->config = is16bit ? ((_mode | (_mode << 8)) | TIMER_CFG_SPLIT_PAIR) : _mode;

    timer_init(tim);
    // register it with the sleep module
    pybsleep_add ((const mp_obj_t)tim, (WakeUpCB_t)timer_init);

    return mp_const_none;

error:
    nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, mpexception_value_invalid_arguments));
}

/// \classmethod \constructor(id, ...)
/// Construct a new timer object of the given id.  If additional
/// arguments are given, then the timer is initialised by `init(...)`.
/// `id` can be 1 to 4
STATIC mp_obj_t pyb_timer_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);

    // create a new Timer object
    int32_t timer_idx = mp_obj_get_int(args[0]) - 1;
    if (timer_idx < 0 || timer_idx > (PYBTIMER_NUM_TIMERS - 1)) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, mpexception_os_resource_not_avaliable));
    }

    pyb_timer_obj_t *tim = &pyb_timer_obj[timer_idx];
    tim->base.type = &pyb_timer_type;
    tim->id = timer_idx;

    if (n_args > 1 || n_kw > 0) {
        // start the peripheral
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
        pyb_timer_init_helper(tim, n_args - 1, args + 1, &kw_args);
    }
    return (mp_obj_t)tim;
}

// \method init()
/// initializes the timer
STATIC mp_obj_t pyb_timer_init(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return pyb_timer_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_timer_init_obj, 1, pyb_timer_init);

// \method deinit()
/// disables the timer
STATIC mp_obj_t pyb_timer_deinit(mp_obj_t self_in) {
    pyb_timer_obj_t *self = self_in;
    timer_disable(self);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_timer_deinit_obj, pyb_timer_deinit);

/// \method channel(channel, *, freq, polarity, duty_cycle)
/// Initialise the timer channel. Initialization requires at least a frequency param. With no
/// extra params given besides the channel id, the channel is returned with the previous configuration
/// os 'None', if it hasn't been initialized before.
///
///     tim1.channel(Timer.A, freq=1000)  # set channel A frequency to 1KHz
///     tim2.channel(Timer.AB, freq=10)   # both channels (because it's a 32 bit timer) combined to create a 10Hz timer
///
///     when initialiazing the channel of a 32-bit timer, channel ID MUST be = Timer.AB
///
/// Keyword arguments:
///
///   - `freq` - specifies the frequency in Hz
///
///   - `polarity` - in PWM specifies the polarity of the pulse. In capture mode specifies the edge to capture.
///                  in order to capture on both negative and positive edges, make it = Timer.POSITIVE | Timer.NEGATIVE.
///
STATIC mp_obj_t pyb_timer_channel(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_freq,                MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_polarity,            MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = PYBTIMER_POLARITY_POS} },
        { MP_QSTR_duty_cycle,          MP_ARG_KW_ONLY  | MP_ARG_INT, {.u_int = 0} },
    };

    pyb_timer_obj_t *tim = pos_args[0];
    mp_int_t channel_n = mp_obj_get_int(pos_args[1]);

    // verify that the timer has been already initialized
    if (!tim->config) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, mpexception_os_request_not_possible));
    }
    if (channel_n != TIMER_A && channel_n != TIMER_B && channel_n != (TIMER_A | TIMER_B)) {
        // invalid channel
        goto error;
    }
    if (channel_n == (TIMER_A | TIMER_B) && (tim->config & TIMER_CFG_SPLIT_PAIR)) {
        // 32-bit channel selected when the timer is in 16-bit mode
        goto error;
    }

    // if only the channel number is given return the previously
    // allocated channel (or None if no previous channel)
    if (n_args == 2 && kw_args->used == 0) {
        pyb_timer_channel_obj_t *ch;
        if ((ch = pyb_timer_channel_find(tim->timer, channel_n))) {
            return ch;
        }
        return mp_const_none;
    }

    // parse the arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 2, pos_args + 2, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // check the frequency
    if (args[0].u_int <= 0) {
        goto error;
    }
    // check that the polarity is not both in pwm mode
    if ((tim->config & TIMER_A) == TIMER_CFG_A_PWM && args[1].u_int == (PYBTIMER_POLARITY_POS | PYBTIMER_POLARITY_NEG)) {
        goto error;
    }

    // allocate a new timer channel
    pyb_timer_channel_obj_t *ch = m_new_obj(pyb_timer_channel_obj_t);
    ch->base.type = &pyb_timer_channel_type;
    ch->timer = tim;
    ch->channel = channel_n;

    // get the frequency the polarity and the duty cycle
    ch->frequency = args[0].u_int;
    ch->polarity = args[1].u_int;
    ch->duty_cycle = MIN(100, MAX(0, args[2].u_int));

    timer_channel_init(ch);

    // register it with the sleep module
    pybsleep_add ((const mp_obj_t)ch, (WakeUpCB_t)timer_channel_init);

    // add the timer to the list
    pyb_timer_channel_add(ch);

    return ch;

error:
    nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, mpexception_value_invalid_arguments));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_timer_channel_obj, 2, pyb_timer_channel);

STATIC const mp_map_elem_t pyb_timer_locals_dict_table[] = {
    // instance methods
    { MP_OBJ_NEW_QSTR(MP_QSTR_init),                    (mp_obj_t)&pyb_timer_init_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_deinit),                  (mp_obj_t)&pyb_timer_deinit_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_channel),                 (mp_obj_t)&pyb_timer_channel_obj },

    // class constants
    { MP_OBJ_NEW_QSTR(MP_QSTR_A),                       MP_OBJ_NEW_SMALL_INT(TIMER_A) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_B),                       MP_OBJ_NEW_SMALL_INT(TIMER_B) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_AB),                      MP_OBJ_NEW_SMALL_INT(TIMER_A | TIMER_B) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ONE_SHOT),                MP_OBJ_NEW_SMALL_INT(TIMER_CFG_A_ONE_SHOT) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_PERIODIC),                MP_OBJ_NEW_SMALL_INT(TIMER_CFG_A_PERIODIC) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_EDGE_COUNT),              MP_OBJ_NEW_SMALL_INT(TIMER_CFG_A_CAP_COUNT) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_EDGE_TIME),               MP_OBJ_NEW_SMALL_INT(TIMER_CFG_A_CAP_TIME) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_PWM),                     MP_OBJ_NEW_SMALL_INT(TIMER_CFG_A_PWM) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_POSITIVE),                MP_OBJ_NEW_SMALL_INT(PYBTIMER_POLARITY_POS) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_NEGATIVE),                MP_OBJ_NEW_SMALL_INT(PYBTIMER_POLARITY_NEG) },
};
STATIC MP_DEFINE_CONST_DICT(pyb_timer_locals_dict, pyb_timer_locals_dict_table);

const mp_obj_type_t pyb_timer_type = {
    { &mp_type_type },
    .name = MP_QSTR_Timer,
    .print = pyb_timer_print,
    .make_new = pyb_timer_make_new,
    .locals_dict = (mp_obj_t)&pyb_timer_locals_dict,
};

STATIC const mp_cb_methods_t pyb_timer_channel_cb_methods = {
    .init = pyb_timer_channel_callback,
    .enable = pyb_timer_channel_callback_enable,
    .disable = pyb_timer_channel_callback_disable,
};

STATIC void TIMERGenericIntHandler(uint32_t timer, uint16_t channel) {
    pyb_timer_channel_obj_t *self;
    uint32_t status;

    if ((self = pyb_timer_channel_find(timer, channel))) {
        status = MAP_TimerIntStatus(self->timer->timer, true) & self->channel;
        MAP_TimerIntClear(self->timer->timer, status);
        mp_obj_t _callback = mpcallback_find(self);
        mpcallback_handler(_callback);
    }
}

STATIC void TIMER0AIntHandler(void) {
    TIMERGenericIntHandler(TIMERA0_BASE, TIMER_A);
}

STATIC void TIMER0BIntHandler(void) {
    TIMERGenericIntHandler(TIMERA0_BASE, TIMER_B);
}

STATIC void TIMER1AIntHandler(void) {
    TIMERGenericIntHandler(TIMERA1_BASE, TIMER_A);
}

STATIC void TIMER1BIntHandler(void) {
    TIMERGenericIntHandler(TIMERA1_BASE, TIMER_B);
}

STATIC void TIMER2AIntHandler(void) {
    TIMERGenericIntHandler(TIMERA2_BASE, TIMER_A);
}

STATIC void TIMER2BIntHandler(void) {
    TIMERGenericIntHandler(TIMERA2_BASE, TIMER_B);
}

STATIC void TIMER3AIntHandler(void) {
    TIMERGenericIntHandler(TIMERA3_BASE, TIMER_A);
}

STATIC void TIMER3BIntHandler(void) {
    TIMERGenericIntHandler(TIMERA3_BASE, TIMER_B);
}

STATIC void pyb_timer_channel_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_timer_channel_obj_t *ch = self_in;
    char *ch_id = "AB";
    // timer channel
    if (ch->channel == TIMER_A) {
        ch_id = "A";
    }
    else if (ch->channel == TIMER_B) {
        ch_id = "B";
    }

    mp_printf(print, "<%q %s, timer=%u, %q=%u", MP_QSTR_TimerChannel,
              ch_id, (ch->timer->id + 1), MP_QSTR_freq, ch->frequency);

    uint32_t mode = ch->timer->config & 0xFF;
    if (mode == TIMER_CFG_A_CAP_COUNT || mode == TIMER_CFG_A_CAP_TIME || mode == TIMER_CFG_A_PWM) {
        mp_printf(print, ", %q=Timer.", MP_QSTR_polarity);
        switch (ch->polarity) {
            case PYBTIMER_POLARITY_POS:
                mp_printf(print, "POSITIVE");
                break;
            case PYBTIMER_POLARITY_NEG:
                mp_printf(print, "NEGATIVE");
                break;
            default:
                mp_printf(print, "BOTH");
                break;
        }
        if (mode == TIMER_CFG_A_PWM) {
            mp_printf(print, ", %q=%u", MP_QSTR_duty_cycle, ch->duty_cycle);
        }
    }
    mp_printf(print, ">");
}

/// \method freq([value])
/// get or set the frequency of the timer channel
STATIC mp_obj_t pyb_timer_channel_freq(mp_uint_t n_args, const mp_obj_t *args) {
    pyb_timer_channel_obj_t *ch = args[0];
    if (n_args == 1) {
        // get
        return mp_obj_new_int(ch->frequency);
    } else {
        // set
        ch->frequency = mp_obj_get_int(args[1]);
        timer_channel_init(ch);
        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_channel_freq_obj, 1, 2, pyb_timer_channel_freq);

/// \method time([value])
/// get or set the value of the timer channel in microseconds
STATIC mp_obj_t pyb_timer_channel_time(mp_uint_t n_args, const mp_obj_t *args) {
    pyb_timer_channel_obj_t *ch = args[0];
    uint32_t value;
    // calculate the period, the prescaler and the match value
    uint32_t period;
    uint32_t match;
    (void)compute_prescaler_period_and_match_value(ch, &period, &match);
    if (n_args == 1) {
        // get
        value = (ch->channel == TIMER_B) ? HWREG(ch->timer->timer + TIMER_O_TBV) : HWREG(ch->timer->timer + TIMER_O_TAV);
        // return the current timer value in microseconds
        // substract value to period since we are always operating in count-down mode
        uint32_t time_t = (1000 * (period - value)) / period;
        return mp_obj_new_int((time_t * 1000) / ch->frequency);
    }
    else {
        // set
        value = (mp_obj_get_int(args[1]) * ((ch->frequency * period) / 1000)) / 1000;
        if ((value > 0xFFFF) && (ch->timer->config & TIMER_CFG_SPLIT_PAIR)) {
            // this exceeds the maximum value of a 16-bit timer
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, mpexception_value_invalid_arguments));
        }
        // write period minus value since we are always operating in count-down mode
        TimerValueSet (ch->timer->timer, ch->channel, (period - value));
        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_channel_time_obj, 1, 2, pyb_timer_channel_time);

/// \method event_count()
/// get the number of events triggered by the configured edge
STATIC mp_obj_t pyb_timer_channel_event_count(mp_obj_t self_in) {
    pyb_timer_channel_obj_t *ch = self_in;
    return mp_obj_new_int(MAP_TimerValueGet(ch->timer->timer, ch->channel == (TIMER_A | TIMER_B) ? TIMER_A : ch->channel));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_timer_channel_event_count_obj, pyb_timer_channel_event_count);

/// \method event_time()
/// get the time at which the last event was triggered
STATIC mp_obj_t pyb_timer_channel_event_time(mp_obj_t self_in) {
    pyb_timer_channel_obj_t *ch = self_in;
    // calculate the period, the prescaler and the match value
    uint32_t period;
    uint32_t match;
    (void)compute_prescaler_period_and_match_value(ch, &period, &match);
    uint32_t value = MAP_TimerValueGet(ch->timer->timer, ch->channel == (TIMER_A | TIMER_B) ? TIMER_A : ch->channel);
    // substract value to period since we are always operating in count-down mode
    uint32_t time_t = (1000 * (period - value)) / period;
    return mp_obj_new_int((time_t * 1000) / ch->frequency);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_timer_channel_event_time_obj, pyb_timer_channel_event_time);

/// \method duty_cycle()
/// get or set the duty cycle when in PWM mode
STATIC mp_obj_t pyb_timer_channel_duty_cycle(mp_uint_t n_args, const mp_obj_t *args) {
    pyb_timer_channel_obj_t *ch = args[0];
    if (n_args == 1) {
        // get
        return mp_obj_new_int(ch->duty_cycle);
    }
    else {
        // duty cycle must be converted from percentage to ticks
        // calculate the period, the prescaler and the match value
        uint32_t period;
        uint32_t match;
        ch->duty_cycle = MIN(100, MAX(0, mp_obj_get_int(args[1])));
        compute_prescaler_period_and_match_value(ch, &period, &match);
        if (n_args == 3) {
            // set the new polarity if requested
            ch->polarity = mp_obj_get_int(args[2]);
            MAP_TimerControlLevel(ch->timer->timer, ch->channel, (ch->polarity == PYBTIMER_POLARITY_NEG) ? true : false);
        }
        MAP_TimerMatchSet(ch->timer->timer, ch->channel, match);
        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pyb_timer_channel_duty_cycle_obj, 1, 3, pyb_timer_channel_duty_cycle);

/// \method callback(handler, value, priority)
/// create a callback object associated with the timer channel
STATIC mp_obj_t pyb_timer_channel_callback (mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_arg_val_t args[mpcallback_INIT_NUM_ARGS];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, mpcallback_INIT_NUM_ARGS, mpcallback_init_args, args);

    pyb_timer_channel_obj_t *ch = pos_args[0];
    mp_obj_t _callback = mpcallback_find(ch);
    if (kw_args->used > 0 || !_callback) {
        // convert the priority to the correct value
        uint priority = mpcallback_translate_priority (args[2].u_int);

        // validate the power mode
        uint pwrmode = args[4].u_int;
        if (pwrmode != PYB_PWR_MODE_ACTIVE) {
            goto invalid_args;
        }

        // disable the callback first
        pyb_timer_channel_callback_disable(ch);

        uint32_t _config = (ch->channel == TIMER_B) ? ((ch->timer->config & TIMER_B) >> 8) : (ch->timer->config & TIMER_A);
        uint8_t shift = (ch->channel == TIMER_B) ? 8 : 0;
        switch (_config) {
        case TIMER_CFG_A_ONE_SHOT:
        case TIMER_CFG_A_PERIODIC:
            ch->timer->intflags |= TIMER_TIMA_TIMEOUT << shift;
            break;
        case TIMER_CFG_A_CAP_COUNT:
            ch->timer->intflags |= TIMER_CAPA_MATCH << shift;
            break;
        case TIMER_CFG_A_CAP_TIME:
            ch->timer->intflags |= TIMER_CAPA_EVENT << shift;
            break;
        case TIMER_CFG_A_PWM:
            // special case for the match interrupt
            ch->timer->intflags |= ((ch->channel & TIMER_A) == TIMER_A) ? TIMER_TIMA_MATCH : TIMER_TIMB_MATCH;
            break;
        default:
            break;
        }
        if (ch->channel == (TIMER_A | TIMER_B)) {
            // again a special case for the pwm match interrupt
            if (_config == TIMER_CFG_A_PWM) {
                ch->timer->intflags |= TIMER_TIMB_MATCH;
            } else {
                ch->timer->intflags |= (ch->timer->intflags << 8);
            }
        }

        void (*pfnHandler)(void);
        uint32_t intregister;
        switch (ch->timer->timer) {
        case TIMERA0_BASE:
            if (ch->channel == TIMER_B) {
                pfnHandler = &TIMER0BIntHandler;
                intregister = INT_TIMERA0B;
            } else {
                pfnHandler = &TIMER0AIntHandler;
                intregister = INT_TIMERA0A;
            }
            break;
        case TIMERA1_BASE:
            if (ch->channel == TIMER_B) {
                pfnHandler = &TIMER1BIntHandler;
                intregister = INT_TIMERA1B;
            } else {
                pfnHandler = &TIMER1AIntHandler;
                intregister = INT_TIMERA1A;
            }
            break;
        case TIMERA2_BASE:
            if (ch->channel == TIMER_B) {
                pfnHandler = &TIMER2BIntHandler;
                intregister = INT_TIMERA2B;
            } else {
                pfnHandler = &TIMER2AIntHandler;
                intregister = INT_TIMERA2A;
            }
            break;
        default:
            if (ch->channel == TIMER_B) {
                pfnHandler = &TIMER3BIntHandler;
                intregister = INT_TIMERA3B;
            } else {
                pfnHandler = &TIMER3AIntHandler;
                intregister = INT_TIMERA3A;
            }
            break;
        }

        // register the interrupt and configure the priority
        MAP_IntPrioritySet(intregister, priority);
        MAP_TimerIntRegister(ch->timer->timer, ch->channel, pfnHandler);

        // create the callback
        _callback = mpcallback_new (ch, args[1].u_obj, &pyb_timer_channel_cb_methods);

        // get the value if given
        uint32_t c_value = MAX(0, args[3].u_int);
        ch->duty_cycle = MIN(100, c_value);

        // reload the timer
        uint32_t period;
        uint32_t match;
        compute_prescaler_period_and_match_value(ch, &period, &match);
        MAP_TimerLoadSet(ch->timer->timer, ch->channel, period);
        // set the appropiate match value
        MAP_TimerMatchSet(ch->timer->timer, ch->channel, (_config == TIMER_CFG_A_PWM) ? match : c_value);

        // enable the callback before returning
        pyb_timer_channel_callback_enable(ch);
    }
    return _callback;

invalid_args:
    nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, mpexception_value_invalid_arguments));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_timer_channel_callback_obj, 1, pyb_timer_channel_callback);

STATIC const mp_map_elem_t pyb_timer_channel_locals_dict_table[] = {
    // instance methods
    { MP_OBJ_NEW_QSTR(MP_QSTR_freq),                 (mp_obj_t)&pyb_timer_channel_freq_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_time),                 (mp_obj_t)&pyb_timer_channel_time_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_event_count),          (mp_obj_t)&pyb_timer_channel_event_count_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_event_time),           (mp_obj_t)&pyb_timer_channel_event_time_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_duty_cycle),           (mp_obj_t)&pyb_timer_channel_duty_cycle_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_callback),             (mp_obj_t)&pyb_timer_channel_callback_obj },
};
STATIC MP_DEFINE_CONST_DICT(pyb_timer_channel_locals_dict, pyb_timer_channel_locals_dict_table);

STATIC const mp_obj_type_t pyb_timer_channel_type = {
    { &mp_type_type },
    .name = MP_QSTR_TimerChannel,
    .print = pyb_timer_channel_print,
    .locals_dict = (mp_obj_t)&pyb_timer_channel_locals_dict,
};

