# can_device

A SocketCAN interface on its own event thread, for drivers that talk to a device with cyclic frames.

`can_device::CanDevice` opens the interface with libio's `socket_can`, reopens it every second while
it is on error (a missing interface at startup included), and calls the subclass on the CAN thread:

- `rx_handler` for every received frame
- `tick_handler` every tick interval, also while the link is down
- `link_handler` when the interface comes up or is lost

`tx_frame` is called from those handlers, on the CAN thread. It returns false and drops the frame
while the link is down or while the previous frame has not left the socket, so a bus that stops
acknowledging does not pile up stale frames.

Used by `InfyPower_BEG1K075G`.
