//! Portable native item instances. Native InventoryProfile owns slot/equipment
//! rules; Rust owns the complete supported snapshot and refuses partial trees.
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Item {
    pub parent: i32,
    pub kind: u8,
    pub slot: i16,
    pub id: u32,
    pub charges: i16,
    pub color: u32,
    pub custom: String,
    pub attuned: bool,
    pub ornament_icon: u32,
    pub ornament_file: u32,
    pub ornament_model: u32,
    pub serial: i32,
    pub recast: u32,
    pub scaling: bool,
    pub scaling_exp: u32,
    pub evolve_id: u64,
    pub evolve_character: u32,
    pub evolve_item: u32,
    pub evolve_final: u32,
    pub evolve_amount: u64,
    pub evolve_progress: f64,
    pub evolve_active: bool,
    pub evolve_equipped: bool,
}

pub fn valid(items: &[Item]) -> bool {
    if items.len() > 512 {
        return false;
    }
    let mut slots = BTreeSet::new();
    let mut serials = BTreeSet::new();
    let mut depths = Vec::with_capacity(items.len());
    let mut cursor = 0;
    for (i, item) in items.iter().enumerate() {
        if item.id == 0
            || item.custom.len() > 4096
            || !item.evolve_progress.is_finite()
            || !slots.insert((item.parent, item.kind, item.slot))
            || (item.serial != 0 && !serials.insert(item.serial))
        {
            return false;
        }
        let depth = match item.kind {
            0 | 3 if item.parent == -1 && item.slot >= 0 => 0,
            1 | 2 if item.parent >= 0 && (item.parent as usize) < i => {
                if item.slot < 0 || item.slot >= if item.kind == 1 { 10 } else { 6 } {
                    return false;
                }
                depths[item.parent as usize] + 1
            }
            _ => return false,
        };
        if depth > 3 {
            return false;
        }
        if item.kind == 3 {
            if item.slot != cursor {
                return false;
            }
            cursor += 1;
        }
        depths.push(depth);
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    fn item(parent: i32, kind: u8, slot: i16, serial: i32) -> Item {
        Item {
            parent,
            kind,
            slot,
            serial,
            id: 1001,
            charges: -1,
            ..Item::default()
        }
    }
    #[test]
    fn bags_augments_and_ordered_cursor_are_one_complete_tree() {
        let tree = vec![
            item(-1, 0, 23, 1),
            item(0, 1, 0, 2),
            item(1, 2, 5, 3),
            item(-1, 3, 0, 4),
            item(-1, 3, 1, 5),
        ];
        assert!(valid(&tree));
        for index in 0..tree.len() {
            let mut broken = tree.clone();
            broken[index].parent = index as i32;
            assert!(!valid(&broken));
        }
        let mut duplicate = tree.clone();
        duplicate.push(tree[1].clone());
        assert!(!valid(&duplicate));
        let mut cursor_gap = tree.clone();
        cursor_gap[4].slot = 2;
        assert!(!valid(&cursor_gap));
        let mut nonfinite = tree.clone();
        nonfinite[1].evolve_progress = f64::NAN;
        assert!(!valid(&nonfinite));
        let mut hidden = tree.clone();
        hidden[1].slot = 10;
        assert!(!valid(&hidden));
    }
}
