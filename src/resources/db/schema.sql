-- MixedEditing PC 端剪辑数据库 schema（SQLite）
-- 见 docs/数据库设计.md 第 4 章。所有时间字段：unix 毫秒；时长/位置：微秒(_us)。
-- 本脚本可重复执行（IF NOT EXISTS），由 Database 在迁移时整体运行。

-- ============ 账户与工程 ============
CREATE TABLE IF NOT EXISTS account (
    id              TEXT PRIMARY KEY,
    username        TEXT NOT NULL,
    email           TEXT,
    avatar_url      TEXT,
    auth_token      TEXT,
    token_expire_at INTEGER,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL,
    deleted_at      INTEGER,
    rev             INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE IF NOT EXISTS project (
    id           TEXT PRIMARY KEY,
    account_id   TEXT NOT NULL REFERENCES account(id),
    title        TEXT NOT NULL DEFAULT '未命名工程',
    description  TEXT,
    cover_path   TEXT,
    width        INTEGER NOT NULL DEFAULT 1920,
    height       INTEGER NOT NULL DEFAULT 1080,
    fps          REAL    NOT NULL DEFAULT 30.0,
    sample_rate  INTEGER NOT NULL DEFAULT 48000,
    duration_us  INTEGER NOT NULL DEFAULT 0,
    settings     TEXT,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,
    rev          INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_project_account ON project(account_id) WHERE deleted_at IS NULL;

-- ============ 素材库 ============
CREATE TABLE IF NOT EXISTS media_asset (
    id              TEXT PRIMARY KEY,
    project_id      TEXT NOT NULL REFERENCES project(id),
    media_type      TEXT NOT NULL CHECK (media_type IN ('video','audio','image')),
    source_kind     TEXT NOT NULL CHECK (source_kind IN ('local','kaiyan','cloud')),
    file_path       TEXT,
    remote_url      TEXT,
    kaiyan_video_id INTEGER,
    display_name    TEXT NOT NULL,
    duration_us     INTEGER NOT NULL DEFAULT 0,
    width           INTEGER,
    height          INTEGER,
    fps             REAL,
    sample_rate     INTEGER,
    channels        INTEGER,
    codec           TEXT,
    file_size       INTEGER,
    checksum        TEXT,
    thumbnail_path  TEXT,
    proxy_path      TEXT,
    waveform_path   TEXT,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL,
    deleted_at      INTEGER,
    rev             INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_asset_project  ON media_asset(project_id) WHERE deleted_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_asset_checksum ON media_asset(checksum);

-- ============ 时间线：序列 / 轨道 / 片段 ============
CREATE TABLE IF NOT EXISTS sequence (
    id           TEXT PRIMARY KEY,
    project_id   TEXT NOT NULL REFERENCES project(id),
    name         TEXT NOT NULL DEFAULT '时间线 1',
    is_main      INTEGER NOT NULL DEFAULT 1,
    duration_us  INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,
    rev          INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_sequence_project ON sequence(project_id) WHERE deleted_at IS NULL;

CREATE TABLE IF NOT EXISTS track (
    id           TEXT PRIMARY KEY,
    sequence_id  TEXT NOT NULL REFERENCES sequence(id),
    track_type   TEXT NOT NULL CHECK (track_type IN ('video','audio','text')),
    track_index  INTEGER NOT NULL,
    name         TEXT,
    is_muted     INTEGER NOT NULL DEFAULT 0,
    is_locked    INTEGER NOT NULL DEFAULT 0,
    is_hidden    INTEGER NOT NULL DEFAULT 0,
    volume       REAL    NOT NULL DEFAULT 1.0,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,
    rev          INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_track_sequence ON track(sequence_id) WHERE deleted_at IS NULL;

CREATE TABLE IF NOT EXISTS clip (
    id                TEXT PRIMARY KEY,
    track_id          TEXT NOT NULL REFERENCES track(id),
    asset_id          TEXT REFERENCES media_asset(id),
    kind              TEXT NOT NULL CHECK (kind IN ('video','audio','image','text')),
    timeline_start_us INTEGER NOT NULL,
    duration_us       INTEGER NOT NULL,
    source_in_us      INTEGER NOT NULL DEFAULT 0,
    source_out_us     INTEGER NOT NULL DEFAULT 0,
    speed             REAL NOT NULL DEFAULT 1.0,
    opacity           REAL NOT NULL DEFAULT 1.0,
    volume            REAL NOT NULL DEFAULT 1.0,
    transform         TEXT,
    is_enabled        INTEGER NOT NULL DEFAULT 1,
    created_at        INTEGER NOT NULL,
    updated_at        INTEGER NOT NULL,
    deleted_at        INTEGER,
    rev               INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_clip_track ON clip(track_id, timeline_start_us) WHERE deleted_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_clip_asset ON clip(asset_id);

-- ============ 特效 / 转场 / 关键帧 / 文字 ============
CREATE TABLE IF NOT EXISTS effect (
    id           TEXT PRIMARY KEY,
    owner_type   TEXT NOT NULL CHECK (owner_type IN ('clip','track')),
    owner_id     TEXT NOT NULL,
    effect_type  TEXT NOT NULL,
    order_index  INTEGER NOT NULL DEFAULT 0,
    is_enabled   INTEGER NOT NULL DEFAULT 1,
    params       TEXT,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL,
    deleted_at   INTEGER,
    rev          INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_effect_owner ON effect(owner_type, owner_id) WHERE deleted_at IS NULL;

CREATE TABLE IF NOT EXISTS transition (
    id              TEXT PRIMARY KEY,
    track_id        TEXT NOT NULL REFERENCES track(id),
    from_clip_id    TEXT REFERENCES clip(id),
    to_clip_id      TEXT REFERENCES clip(id),
    transition_type TEXT NOT NULL,
    duration_us     INTEGER NOT NULL,
    params          TEXT,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL,
    deleted_at      INTEGER,
    rev             INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_transition_track ON transition(track_id) WHERE deleted_at IS NULL;

CREATE TABLE IF NOT EXISTS keyframe (
    id             TEXT PRIMARY KEY,
    owner_type     TEXT NOT NULL CHECK (owner_type IN ('clip','effect')),
    owner_id       TEXT NOT NULL,
    property_name  TEXT NOT NULL,
    time_offset_us INTEGER NOT NULL,
    value          TEXT NOT NULL,
    interpolation  TEXT NOT NULL DEFAULT 'linear',
    bezier_handles TEXT,
    created_at     INTEGER NOT NULL,
    updated_at     INTEGER NOT NULL,
    deleted_at     INTEGER,
    rev            INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_keyframe_owner
    ON keyframe(owner_type, owner_id, property_name, time_offset_us)
    WHERE deleted_at IS NULL;

CREATE TABLE IF NOT EXISTS text_element (
    clip_id     TEXT PRIMARY KEY REFERENCES clip(id),
    content     TEXT NOT NULL,
    font_family TEXT,
    font_size   REAL,
    color       TEXT,
    alignment   TEXT,
    style       TEXT,
    updated_at  INTEGER NOT NULL,
    rev         INTEGER NOT NULL DEFAULT 1
);

-- ============ 导出任务（本地执行，不同步） ============
CREATE TABLE IF NOT EXISTS export_job (
    id            TEXT PRIMARY KEY,
    project_id    TEXT NOT NULL REFERENCES project(id),
    sequence_id   TEXT NOT NULL REFERENCES sequence(id),
    status        TEXT NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending','running','done','failed','canceled')),
    output_path   TEXT,
    format        TEXT NOT NULL DEFAULT 'mp4',
    video_codec   TEXT NOT NULL DEFAULT 'libx264',
    audio_codec   TEXT NOT NULL DEFAULT 'aac',
    width         INTEGER,
    height        INTEGER,
    fps           REAL,
    bitrate       INTEGER,
    progress      REAL NOT NULL DEFAULT 0.0,
    error_message TEXT,
    created_at    INTEGER NOT NULL,
    started_at    INTEGER,
    finished_at   INTEGER
);
CREATE INDEX IF NOT EXISTS idx_export_project ON export_job(project_id, status);

-- ============ 同步基础设施 ============
CREATE TABLE IF NOT EXISTS device (
    id           TEXT PRIMARY KEY,
    account_id   TEXT NOT NULL REFERENCES account(id),
    device_name  TEXT,
    platform     TEXT,
    last_sync_at INTEGER,
    created_at   INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS sync_oplog (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    table_name  TEXT NOT NULL,
    row_id      TEXT NOT NULL,
    op          TEXT NOT NULL CHECK (op IN ('insert','update','delete')),
    payload     TEXT,
    rev         INTEGER NOT NULL,
    created_at  INTEGER NOT NULL,
    synced      INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_oplog_unsynced ON sync_oplog(synced, id);
