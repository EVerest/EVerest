-- Tag persisted messages with the OCPP connection they belong to, so that a charging station with
-- more than one connection does not replay another connection's messages on boot, and so that
-- removing a message only affects the queue it came from.
-- NULL means the primary (CSMS) connection, which is what every pre-existing row is.
ALTER TABLE TRANSACTION_QUEUE ADD COLUMN CONNECTION TEXT;
ALTER TABLE NORMAL_QUEUE ADD COLUMN CONNECTION TEXT;
