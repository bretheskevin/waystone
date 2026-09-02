use alloc::vec::Vec;

pub mod checkpoint;
pub(crate) mod folder_layout;
pub mod jksv;
pub mod mgba;
pub mod twilight;

use crate::model::{NormalizedSave, RawTree, SystemId};

pub trait Adapter {
    fn id(&self) -> &str;
    fn systems(&self) -> &[SystemId];
    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave>;
    fn to_native(&self, save: &NormalizedSave) -> RawTree;
}
