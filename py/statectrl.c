/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 EcmaXp
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

#include <stdlib.h>
#include <stdio.h>

#include "py/mpstate.h"
#include "py/stackctrl.h"
#include "py/statectrl.h"

/* TODO: make more function for handling nlr_xxx?
   across handling mp_state_ctx will make invaild nlr_top!
   TODO: add 
*/

#if MICROPY_MULTI_STATE_CONTEXT

mp_state_ctx_t *mp_state_new() {
    mp_state_ctx_t *state = calloc(1, sizeof(mp_state_ctx_t));
    assert(state != NULL);
    
    return state;
}

void mp_state_free(mp_state_ctx_t *state) {
    assert(!mp_state_is_loaded(state));
    free(state);
}

void mp_state_load_raw(mp_state_ctx_t *state) {
    // TODO: acquire lock?
    
    nlr_buf_t *nlr_ptr = NULL;
    
    if (mp_state_ctx != NULL) {
        // prev state
        assert(!MP_STATE_VM(is_state_loaded));
        
        nlr_ptr = MP_STATE_VM(nlr_top);
        MP_STATE_VM(nlr_top) = NULL;
    }
    
    // current state
    mp_state_ctx = state;
    MP_STATE_VM(nlr_top) = nlr_ptr;
    
    assert(!MP_STATE_VM(is_state_loaded));
    MP_STATE_VM(is_state_loaded) = true;
}

void mp_state_load(mp_state_ctx_t *state) {
    // this function should keep minimal
    // for setup stack_top at right position.
    mp_state_load_raw(state);
    mp_stack_ctrl_init();
}

void mp_state_force_load(mp_state_ctx_t *state, nlr_buf_t *nlr) {
    if (mp_state_ctx != NULL && mp_state_is_loaded(mp_state_ctx)) {
        if (mp_state_ctx == state) {
            MP_STATE_VM(nlr_top) = nlr;
            return;
        } else {
            MP_STATE_VM(nlr_top) = NULL;
            mp_state_store(mp_state_ctx);
        }
    }
    
    mp_state_load(state);
    MP_STATE_VM(nlr_top) = nlr;
}

void mp_state_store(mp_state_ctx_t *state) {
    // TODO: acquire lock?
    assert(mp_state_ctx != NULL);
    assert(mp_state_ctx == state);
    
    // will prev state
    assert(MP_STATE_VM(is_state_loaded));
    MP_STATE_VM(is_state_loaded) = false;
}

bool mp_state_is_loaded(mp_state_ctx_t *state) {
    return state->vm.is_state_loaded;
}

#endif
