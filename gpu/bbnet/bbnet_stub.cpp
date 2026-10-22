// SPDX-License-Identifier: GPL-2.0-or-later
// BB_NET=OFF (Android): no online module. Network and PSN calls stay on the runtime's offline
// versions (runtime_services.c), which is what FEXCore builds always use.
extern "C" void bbnet_configure(const char*, const char*, const char*, const char*) {}
extern "C" void bbnet_register(void) {}
