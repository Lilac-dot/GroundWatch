-- GroundWatch database (Supabase project "groundwatch", ap-south-1).
-- Applied as migration create_events_and_access. Kept here for reference.
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
    label        text        -- ground-truth tag from --label, for tuning and ML training
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

-- Give a teammate dashboard access (run in the Supabase SQL editor):
--   insert into private.dashboard_users (email) values ('name@example.com');
