-- GroundWatch database (Supabase project "groundwatch", ap-south-1).
-- Applied as migrations create_events_and_access, add_gap_and_noise_features
-- and add_minutely_view.
-- Kept here for reference.
--
-- Access model:
--   bridge (tools/groundwatch_pc.py --cloud) writes with the secret key from a local .env;
--     the secret key bypasses RLS and must never be committed.
--   dashboard reads with the publishable key, signed in; only emails listed in
--     private.dashboard_users can read. Nobody can insert, update or delete through the API.

create table public.events (
    id           bigint generated always as identity primary key,
    created_at   timestamptz not null default now(),
    device       text        not null default 'gw-01',
    event        text        not null check (event in ('NONE','NOISE','ANIMAL','PERSON','DIGGING')),
    zone         text        check (zone in ('L','C','R')),
    peak_l       smallint    not null default 0,
    peak_c       smallint    not null default 0,
    peak_r       smallint    not null default 0,
    hits_l       smallint    not null default 0,
    hits_c       smallint    not null default 0,
    hits_r       smallint    not null default 0,
    active_ms_l  integer     not null default 0,
    active_ms_c  integer     not null default 0,
    active_ms_r  integer     not null default 0,
    first_ms_l   integer,
    first_ms_c   integer,
    first_ms_r   integer,
    pir          boolean     not null default false,
    night        boolean     not null default false,
    ldr          smallint,
    label        text,       -- ground-truth tag from --label, for tuning and ML training
    gap_min_ms_l integer,    -- shortest / longest gap between hits (ms), null if < 2 hits
    gap_min_ms_c integer,
    gap_min_ms_r integer,
    gap_max_ms_l integer,
    gap_max_ms_c integer,
    gap_max_ms_r integer,
    noise_l      smallint,   -- background noise: mean |deviation| from baseline, ADC counts
    noise_c      smallint,
    noise_r      smallint
);

create index events_created_at_idx on public.events (created_at desc);
create index events_alarms_idx on public.events (created_at desc) where event in ('PERSON','DIGGING');

create schema if not exists private;
create table private.dashboard_users (
    email text primary key check (email = lower(email))
);

create or replace function private.is_dashboard_user()
returns boolean language sql stable security definer set search_path = '' as $$
    select exists (
        select 1 from private.dashboard_users
        where email = lower(coalesce(auth.jwt() ->> 'email', ''))
    );
$$;
revoke all on function private.is_dashboard_user() from public;
grant usage on schema private to authenticated;
grant execute on function private.is_dashboard_user() to authenticated;

alter table public.events enable row level security;
create policy "team can read events" on public.events for select
    to authenticated using ((select private.is_dashboard_user()));
revoke insert, update, delete on public.events from anon, authenticated;
revoke all on public.events from anon;

alter publication supabase_realtime add table public.events;

-- Per-minute summary for the dashboard (migration add_minutely_view).
-- security_invoker: runs with the reader's rights, so the events RLS applies.
create or replace view public.events_minutely with (security_invoker = true) as
select device, date_trunc('minute', created_at) as minute,
       max(peak_l) as peak_l, max(peak_c) as peak_c, max(peak_r) as peak_r,
       sum(hits_l)::int as hits_l, sum(hits_c)::int as hits_c, sum(hits_r)::int as hits_r,
       round(avg(ldr))::int as ldr, bool_or(night) as night, bool_or(pir) as pir,
       count(*) filter (where event = 'PERSON')::int  as person,
       count(*) filter (where event = 'DIGGING')::int as digging,
       count(*) filter (where event = 'ANIMAL')::int  as animal,
       count(*) filter (where event = 'NOISE')::int   as noise,
       count(*)::int as windows
from public.events group by device, date_trunc('minute', created_at);
revoke all on public.events_minutely from anon;
grant select on public.events_minutely to authenticated;
create index if not exists events_device_time_idx on public.events (device, created_at desc);

-- Give a teammate dashboard access (run in the Supabase SQL editor):
--   insert into private.dashboard_users (email) values ('name@example.com');
