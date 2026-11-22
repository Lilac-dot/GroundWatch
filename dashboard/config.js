// Supabase project for GroundWatch. The publishable key is meant to be public:
// it can't read or write anything by itself. Reads need a signed-in team member
// (row-level security, see database/schema.sql) and writes need the secret key,
// which only the bridge has.
export const SUPABASE_URL = "https://lpxjnqlsdlgynvvygnms.supabase.co";
export const SUPABASE_KEY = "sb_publishable_wC9do2yLB8zcYqLchS8s3A_CEiZLJk4";

export const DEVICES = ["gw-01", "demo"];   // gw-01 = the board, demo = --demo rows
export const ONLINE_SECONDS = 15;           // "online" if a window arrived this recently
export const LDR_DARK = 60;                 // matches src/config.h (night below this)
