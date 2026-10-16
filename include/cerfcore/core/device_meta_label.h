#pragma once
#include <string>

struct DeviceMeta;

/* Human-readable "OS" label from a device's cerf.json meta: the os_name with
   a "(CE {major}.{minor})" tag appended only when the name does not already
   carry that version. The version-in-name test mirrors the launcher's table OS
   column (device_tree.py _os_name_has_version). */
std::string OsDisplayLabel(const DeviceMeta& meta);
