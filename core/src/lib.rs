#![no_std]

#[macro_use]
extern crate alloc;

pub mod adapters;
pub mod conflict;
pub mod crc32;
pub mod crypto;
pub mod dsv;
pub mod iso9660;
pub mod model;
pub mod nds_banner;
pub mod packaging;
pub mod rom_art;
pub mod rom_flavor;
pub mod rom_id;
pub mod rom_pair;
pub mod rom_systems;
pub mod url;
pub mod version;
