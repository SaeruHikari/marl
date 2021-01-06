// Copyright 2021 SaeruHikari.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "marl/debug.h"
#include "marl/memory.h"

#include <functional>
#include <memory>

#include <kernel.h>
#include <sce_fiber.h>
#include <libsysmodule.h>
#include "libdbg.h"

namespace marl {

class OSFiber {
 public:
  inline ~OSFiber() = default;

  // createFiberFromCurrentThread() returns a fiber created from the current
  // thread.
  static inline Allocator::unique_ptr<OSFiber> createFiberFromCurrentThread(
      Allocator* allocator);

  // createFiber() returns a new fiber with the given stack size that will
  // call func when switched to. func() must end by switching back to another
  // fiber, and must not return.
  static inline Allocator::unique_ptr<OSFiber> createFiber(
      Allocator* allocator,
      size_t stackSize,
      const std::function<void()>& func);

  // switchTo() immediately switches execution to the given fiber.
  // switchTo() must be called on the currently executing fiber.
  inline void switchTo(OSFiber*);

 private:
  static inline void run(uint64_t argOnInit, uint64_t argOnRun) __attribute__((noreturn));
  alignas(SCE_FIBER_ALIGNMENT) SceFiber fiber;
  alignas(SCE_FIBER_CONTEXT_ALIGNMENT) char fiberContext[1024 * 4];
  
  bool isFiberFromThread = false;
  std::function<void()> target;
};

__attribute__((noreturn)) void OSFiber::run(uint64_t argOnInit, uint64_t argOnRun) {
  std::function<void()> func;
  OSFiber* self = reinterpret_cast<OSFiber*>(argOnInit);
  while (true) 
  {
    std::swap(func, self->target);
    func();
    if (sceFiberFinalize(&self->fiber) != SCE_OK) {
      fatal("sceFiberFinalize Failed!");
    }
  }
}

Allocator::unique_ptr<OSFiber> OSFiber::createFiberFromCurrentThread(
    Allocator* allocator) {
  auto out = allocator->make_unique<OSFiber>();
  out->isFiberFromThread = true;

  return out;
}

Allocator::unique_ptr<OSFiber> OSFiber::createFiber(
    Allocator* allocator,
    size_t stackSize,
    const std::function<void()>& func) {
  auto out = allocator->make_unique<OSFiber>();
  // stackSize is rounded up to the system's allocation granularity (typically
  // 64 KB).
  out->target = func;
  if (sceFiberInitialize(&out->fiber, "", &OSFiber::run, (uint64_t)out.get(),
                         out->fiberContext, 1024 * 4, nullptr) != SCE_OK) {
    fatal("sceFiberInitialize Failed!");
  }
  //out->fiber = CreateFiberEx(stackSize - 1, stackSize, FIBER_FLAG_FLOAT_SWITCH,
  //                           &OSFiber::run, out.get());
  return out;
}

void OSFiber::switchTo(OSFiber* to) {
  if (isFiberFromThread && to->isFiberFromThread) {
    fatal("Can't switch to another thread on a thread!");
  }
  else if (isFiberFromThread && !to->isFiberFromThread) {
    auto ret = sceFiberRun(&to->fiber, 0, nullptr);
    if (ret != SCE_OK) {
      fatal("sceFiberRun Failed!");
    }
  } 
  else if (to->isFiberFromThread) {
    auto ret = sceFiberReturnToThread(0, nullptr);
    if (ret != SCE_OK) {
      fatal("sceFiberReturnToThread Failed!");
    }
  }
  else {
    auto ret = sceFiberSwitch(&to->fiber, 0, nullptr);
    if (ret != SCE_OK) {
      fatal("sceFiberSwitch Failed!");
    }
  }
}
}  // namespace marl
