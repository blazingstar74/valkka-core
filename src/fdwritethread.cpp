/*
 * fdwritethread.cpp :
 * 
 * Copyright 2017, 2018 Valkka Security Ltd. and Sampsa Riikonen.
 * 
 * Authors: Sampsa Riikonen <sampsa.riikonen@iki.fi>
 * 
 * This file is part of the Valkka library.
 * 
 * Valkka is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>
 *
 */

/** 
 *  @file    fdwritethread.cpp
 *  @author  Sampsa Riikonen
 *  @date    2017
 *  @version 0.1
 *  
 *  @brief 
 */ 

#include "fdwritethread.h"


FDWrite::FDWrite(FrameFifo& fifo, const FDWriteContext& ctx) : fifo(fifo), ctx(ctx) {
}

FDWrite::~FDWrite() {
}




FDWriteThread::FDWriteThread(const char* name, FrameFifoContext fifo_ctx) : Thread(name), infifo(name,fifo_ctx), infilter(name, &infifo), infilter_block(name, &infifo) {
}
    
    
FDWriteThread::~FDWriteThread() {
}
 
 
void FDWriteThread::run() {
    loop = True;
    Frame *f;
    
    while(loop) { // LOOP
        f = infifo.read(Timeout::fdwritethread);
        if (!f) { // TIMEOUT
            valkkafslogger.log(LogLevel::crazy) << "FDWriteThread: "<< this->name <<" timeout expired!" << std::endl;
        }
        else { // GOT FRAME // this must ALWAYS BE ACCOMPANIED WITH A RECYCLE CALL
            // Handle signal frames
            if (f->getFrameClass()==FrameClass::signal) { // SIGNALFRAME
                SignalFrame *signalframe = static_cast<SignalFrame*>(f);
                
                FDWriteSignalContext *fd_write_signal_ctx = static_cast<FDWriteSignalContext*>(signalframe->custom_signal_ctx);
                
                handleSignal(*fd_write_signal_ctx);
            } // SIGNALFRAME

            infifo.recycle(f);
        } // GOT FRAME
    }
}
    

void FDWriteThread::preRun() {
}
    
void FDWriteThread::postRun() {
}

void FDWriteThread::preJoin() {
}

void FDWriteThread::postJoin() {
}


void FDWriteThread::handleSignal(const FDWriteSignalContext &signal_ctx) {
    const FDWriteContext &ctx = signal_ctx.pars.fd_write_ctx;

    switch (signal_ctx.signal) {
        
        case FDWriteSignal::exit:
            loop = false;
            break;
        
        case FDWriteSignal::register_stream:
            registerStream(ctx);
            break;
        
        case FDWriteSignal::deregister_stream:
            deregisterStream(ctx);
            break;
        
    };
}


int FDWriteThread::safeGetSlot(const SlotNumber slot, FDWrite*& fd_write) { // -1 = out of range, 0 = free, 1 = reserved // &* = modify pointer in-place
    FDWrite* fd_write_;
    
    if (slot>I_MAX_SLOTS) {
        valkkafslogger.log(LogLevel::fatal) << "FDWrite: safeGetSlot: WARNING! Slot number overfow : increase I_MAX_SLOTS in sizes.h" << std::endl;
        return -1;
    }
    
    try {
        fd_write_ = this->slots_[slot];
    }
    catch (std::out_of_range) {
        valkkafslogger.log(LogLevel::debug) << "FDWrite: safeGetSlot : slot " << slot << " is out of range! " << std::endl;
        fd_write_ = NULL;
        return -1;
    }
    if (!fd_write_) {
        valkkafslogger.log(LogLevel::crazy) << "FDWrite: safeGetSlot : nothing at slot " << slot << std::endl;
        fd_write = NULL;
        return 0;
    }
    else {
        valkkafslogger.log(LogLevel::debug) << "FDWrite: safeGetSlot : returning " << slot << std::endl;
        fd_write = fd_write_;
        return 1;
    }
    
}


void FDWriteThread::registerStream(const FDWriteContext &ctx) {
    FDWrite* fd_write;
    
    valkkafslogger.log(LogLevel::crazy) << "FDWriteThread: registerStream" << std::endl;
    switch (safeGetSlot(ctx.slot, fd_write)) {
        case -1: // out of range
            break;
            
        case 0: // slot is free
            this->slots_[ctx.slot] = new FDWrite(infifo, ctx); 
            valkkafslogger.log(LogLevel::debug) << "FDWriteThread: registerStream : rtsp stream registered at slot " << ctx.slot << " with ptr " << this->slots_[ctx.slot] << std::endl;
            break;
            
        case 1: // slot is reserved
            valkkafslogger.log(LogLevel::normal) << "FDWriteThread: registerStream : slot " << ctx.slot << " is reserved! " << std::endl;
            break;
    } // switch
}


void FDWriteThread::deregisterStream(const FDWriteContext &ctx) {
    FDWrite* fd_write;
    
    valkkafslogger.log(LogLevel::crazy) << "FDWriteThread: deregisterStream" << std::endl;
    switch (safeGetSlot(ctx.slot, fd_write)) {
        case -1: // out of range
            break;
        case 0: // slot is free
            valkkafslogger.log(LogLevel::crazy) << "FDWriteThread: deregisterStream : nothing at slot " << ctx.slot << std::endl;
            break;
        case 1: // slot is reserved
            valkkafslogger.log(LogLevel::debug) << "FDWriteThread: deregisterStream : de-registering " << ctx.slot << std::endl;
            // TODO: close stuff
            this->slots_[ctx.slot] = NULL;
            break;
    } // switch
}
    

void FDWriteThread::registerStreamCall(const FDWriteContext &ctx) {
    FDWriteSignalContext signal_ctx;
    FDWriteSignalPars    pars;
    
    // signal parameters
    pars.fd_write_ctx = ctx;
    
    // context for sending the signal
    signal_ctx.signal = FDWriteSignal::register_stream;
    signal_ctx.pars   = pars;
    
    // prepare a signal frame
    SignalFrame f = SignalFrame();
    f.custom_signal_ctx = (void*)&signal_ctx;
    
    // .. and send it to the queue
    infilter.run(&f);
    
    
}
    
void FDWriteThread::deregisterStreamCall(const FDWriteContext &ctx) {
    FDWriteSignalContext signal_ctx;
    FDWriteSignalPars    pars;
    
    // signal parameters
    pars.fd_write_ctx = ctx;
    
    // context for sending the signal
    signal_ctx.signal = FDWriteSignal::deregister_stream;
    signal_ctx.pars   = pars;
    
    // prepare a signal frame
    SignalFrame f = SignalFrame();
    f.custom_signal_ctx = (void*)&signal_ctx;
    
    // .. and send it to the queue
    infilter.run(&f);
}


void FDWriteThread::requestStopCall() {
    if (!this->has_thread) { return; } // thread never started
    if (stop_requested) { return; }    // can be requested only once
    stop_requested = true;

    FDWriteSignalContext signal_ctx;
    FDWriteSignalPars    pars;
    
    // context for sending the signal
    signal_ctx.signal = FDWriteSignal::exit;
    signal_ctx.pars   = pars;
    
    // prepare a signal frame
    SignalFrame f = SignalFrame();
    f.custom_signal_ctx = (void*)&signal_ctx;
    
    // .. and send it to the queue
    infilter.run(&f);
}

    
    
FifoFrameFilter& FDWriteThread::getFrameFilter() {
    return infilter;

}










