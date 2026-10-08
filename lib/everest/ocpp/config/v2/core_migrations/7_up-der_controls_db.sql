CREATE TABLE IF NOT EXISTS DER_CONTROLS (
    -- CiString<36> cap per OCPP 2.1.
    CONTROL_ID TEXT PRIMARY KEY NOT NULL CHECK (length(CONTROL_ID) <= 36),
    IS_DEFAULT INTEGER NOT NULL CHECK (IS_DEFAULT IN (0, 1)),
    CONTROL_TYPE TEXT NOT NULL,
    IS_SUPERSEDED INTEGER NOT NULL DEFAULT 0 CHECK (IS_SUPERSEDED IN (0, 1)),
    PRIORITY INTEGER NOT NULL CHECK (PRIORITY >= 0),
    -- RFC 3339 shape: lex-comparison-safe. LIKE used so '_' is single-char wildcard.
    START_TIME TEXT CHECK (START_TIME IS NULL OR START_TIME LIKE '____-__-__T__:__:__%Z'),
    -- Upper bound matches MAX_DURATION_SECONDS (one year = 86400 * 365) in der_control.cpp.
    DURATION REAL CHECK (DURATION IS NULL OR (DURATION >= 0 AND DURATION <= 31536000)),
    CONTROL_JSON TEXT NOT NULL,
    -- R04.FR.07: when set, this row has been accepted but its supersede of
    -- PENDING_SUPERSEDE_ID is deferred until START_TIME <= now.
    PENDING_SUPERSEDE_ID TEXT,
    -- R04.FR.20/21: 1 once NotifyDERStartStop(started=true) has been emitted for
    -- this row. The scheduled-check pass flips this at the moment START_TIME <= now.
    STARTED_NOTIFIED INTEGER NOT NULL DEFAULT 0 CHECK (STARTED_NOTIFIED IN (0, 1))
);

CREATE INDEX IF NOT EXISTS idx_der_controls_default_type
    ON DER_CONTROLS (IS_DEFAULT, CONTROL_TYPE);

CREATE INDEX IF NOT EXISTS idx_der_controls_is_default
    ON DER_CONTROLS (IS_DEFAULT);
