#pragma once

// Reason passed to the NoInternet screen so it can show the right message.
enum class NoInternetReason {
    NoNetwork,        // No internet connection at the OS level
    ServerUnreachable // Network up but Waystone server can't be reached (forward-compat)
};

inline const char* no_internet_headline(NoInternetReason r) {
    return r == NoInternetReason::NoNetwork
               ? "No internet connection detected."
               : "Can't reach your Waystone server.";
}
inline const char* no_internet_detail(NoInternetReason r) {
    return r == NoInternetReason::NoNetwork
               ? "Check your Wi-Fi settings and try again."
               : "Check the server URL in Settings and try again.";
}

// Returns true when the console has an active internet connection.
// Each platform provides its own implementation (switch/source/net_status.cpp,
// 3ds/source/net_status.cpp).
bool network_available();
