#ifndef SIGNAL_FWD_H_
#define SIGNAL_FWD_H_

#include <cstdint>

namespace core {

template <typename Signature> class Signal;

template <typename Signature> class ScopedConnection;

class SignalHub;

using ConnectionId = uint64_t;

} // namespace core

#endif // SIGNAL_FWD_H_
