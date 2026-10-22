#pragma once

namespace cerfcore {

namespace host_platform {

bool SetCloseOnExec(int fd);
bool SetCloseOnExecNonBlocking(int fd);

}

}
