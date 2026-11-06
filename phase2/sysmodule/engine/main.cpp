#include "background_backend.hpp"
#include "ipc_self_test.hpp"
#include "switify_service.hpp"

#include "NXConsoleLog.h"

#include <curl/curl.h>
#include <switch.h>

#include <reent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <new>

namespace {

constexpr SocketInitConfig SocketConfig = {
    .tcp_tx_buf_size = 0x800,
    .tcp_rx_buf_size = 0x1000,
    .tcp_tx_buf_max_size = 0x2EE0,
    .tcp_rx_buf_max_size = 0x4000,
    // Spotify AP, Web API, and CDN traffic are TCP-only. Reserving UDP
    // buffers and six bsd:u sessions bought us nothing, but the sessions stay
    // claimed for the entire sysmodule lifetime. Nintendo's online emulators
    // need the same service while launching. Match Streamfin's proven
    // background profile: two BSD sessions and no UDP allocation.
    .udp_tx_buf_size = 0,
    .udp_rx_buf_size = 0,
    .sb_efficiency = 2,
    .num_bsd_sessions = 2,
    .bsd_service_type = BsdServiceType_User,
};

Result socketInitResult = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

void abortUnless(Result result) {
  if (R_FAILED(result)) diagAbortWithResult(result);
}

}  // namespace

extern "C" {

uint32_t __nx_applet_type = AppletType_None;
uint32_t __nx_fs_num_sessions = 2;

void __libnx_initheap(void) {
  // malloc's memory comes from _sbrk_r below. This small block only serves
  // libnx's own startup (argv), which carves from the fake heap.
  static char startup[0x1000];
  extern char* fake_heap_start;
  extern char* fake_heap_end;
  fake_heap_start = startup;
  fake_heap_end = startup + sizeof(startup);
}

// The heap is committed in 2 MiB steps (the kernel's heap granularity) as
// malloc asks for it, up to the ceiling. A fixed static heap counted the whole
// ceiling against the system's memory from the start, though playback uses under
// 2 MiB, and the system has little to spare: with 4 MiB fixed, opening HOME
// over a game ran the system services out of memory (FS sessions closed, the
// menu's GPU start hung).
//
// The artwork-enabled service performs authenticated browse requests whose
// transient JSON tree can overlap the player, artwork URL, and browse page.
// Three MiB was sufficient for playback alone but could exhaust the process
// as soon as Home, Library, Search, or a playlist was opened. Keep the Lite
// service at 3 MiB and restore the hardware-proven 4 MiB ceiling here.
// Called with newlib's malloc lock held.
void* _sbrk_r(struct _reent* reent, ptrdiff_t increment) {
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  constexpr size_t Ceiling = 3 * 1024 * 1024;
#else
  constexpr size_t Ceiling = 4 * 1024 * 1024;
#endif
  constexpr size_t Step = 0x200000;
  static char* base = nullptr;
  static size_t committed = 0;
  static size_t used = 0;

  if (increment < 0 && static_cast<size_t>(-increment) > used) {
    reent->_errno = EINVAL;
    return reinterpret_cast<void*>(-1);
  }
  const size_t want = used + increment;
  if (want > committed) {
    const size_t size = std::min((want + Step - 1) & ~(Step - 1), Ceiling);
    void* address = nullptr;
    if (want > Ceiling || R_FAILED(svcSetHeapSize(&address, size))) {
      reent->_errno = ENOMEM;
      return reinterpret_cast<void*>(-1);
    }
    base = static_cast<char*>(address);
    committed = size;
  }
  char* previous = base + used;
  used = want;
  return previous;
}

void __appInit(void) {
  abortUnless(smInitialize());
  abortUnless(fsInitialize());
  abortUnless(fsdevMountSdmc());
  socketInitResult = socketInitialize(&SocketConfig);
}

void __appExit(void) {
  curl_global_cleanup();
  if (R_SUCCEEDED(socketInitResult)) socketExit();
  fsdevUnmountDevice("sdmc");
  fsExit();
  smExit();
}

}  // extern "C"

int main(int, char**) {
  mkdir("/config", 0755);
  mkdir("/config/Switify", 0755);
  nxlog::installBellLogger();
  nxlog::setLogFile("/config/Switify/background.log", 512 * 1024);
  nxlog::setQueueEnabled(false);
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  nxlog::push(
      "Switify Phase 2 background engine 0.5.0-lite1 starting");
#else
  nxlog::push("Switify Phase 2 background engine 0.5.0 starting");
#endif
#ifdef SWITIFY_LIGHTWEIGHT_OVERLAY
  nxlog::push("Optimized profile: 3072 KiB heap, Vorbis-only decoder");
#else
  nxlog::push("Resilient profile: 4096 KiB heap, Vorbis-only decoder");
#endif
  nxlog::push(
      "Compatibility profile: Applet pool, lazy audout, 2 BSD sessions");
  nxlog::push("Network profile: TCP-only; UDP disabled");
  nxlog::pushHeapUsage("startup");

  // ServiceCore owns the fixed-capacity browsing snapshot and is far larger
  // than the 16 KiB main-thread stack. Keep all long-lived engine state on the
  // process heap so adding browse fields cannot silently overflow that stack.
  auto core = std::unique_ptr<switify::phase2::ServiceCore>(
      new (std::nothrow) switify::phase2::ServiceCore());
  if (!core)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
  nxlog::pushHeapUsage("service core ready");
  abortUnless(switify::phase2::switch_service::initialize(*core));

  auto backend = std::unique_ptr<switify::phase2::BackgroundBackend>(
      new (std::nothrow) switify::phase2::BackgroundBackend(*core));
  if (!backend)
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
  bool backendStarted = false;
  if (R_SUCCEEDED(socketInitResult)) {
    backendStarted = backend->start();
    if (!backendStarted) diagAbortWithResult(MAKERESULT(421, 10));
    nxlog::pushHeapUsage("Spotify backend started");
  } else {
    char detail[40];
    std::snprintf(detail, sizeof(detail), "Network unavailable: 0x%08X",
                  socketInitResult);
    core->configureCommands(false, SwitifyIpcCapability_Status, 0);
    core->publishServiceState(SwitifyServiceState_Error, 0);
    core->publishTrack({}, {}, detail, 0);
    nxlog::pushf("Socket initialization failed: 0x%08X", socketInitResult);
  }
  abortUnless(switify::phase2::ipc_self_test::start());

  switify::phase2::switch_service::loop();

  if (backendStarted) backend->stop();
  abortUnless(switify::phase2::switch_service::exit());
  return 0;
}
