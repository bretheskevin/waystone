#pragma once

// Reason passed to the NoInternet screen so it can show the right message.
enum class NoInternetReason {
    NoNetwork,        // No internet connection at the OS level
    ServerUnreachable // Network up but Waystone server can't be reached (forward-compat)
};

// Returns true when the console has an active internet connection.
// Each platform provides its own implementation (switch/source/net_status.cpp,
// 3ds/source/net_status.cpp).
bool network_available();
