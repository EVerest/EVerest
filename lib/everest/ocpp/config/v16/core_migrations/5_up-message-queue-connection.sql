-- Mirrors the OCPP 2.x migration of the same name so that the shared message queue code has one
-- schema to work against. OCPP 1.6 has no secondary connections, so this column stays NULL, which
-- is how the primary (CSMS) connection is recorded.
ALTER TABLE TRANSACTION_QUEUE ADD COLUMN CONNECTION TEXT;
ALTER TABLE NORMAL_QUEUE ADD COLUMN CONNECTION TEXT;
