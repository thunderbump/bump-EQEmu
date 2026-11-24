/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "water_map_v1.h"
#include <cmath>

WaterMapV1::WaterMapV1() {
	BSP_Root = nullptr;
}

WaterMapV1::~WaterMapV1() {
	if (BSP_Root) {
		delete[] BSP_Root;
	}
}

WaterRegionType WaterMapV1::ReturnRegionType(const glm::vec3& location) const {
	return BSPReturnRegionType(1, glm::vec3(location.y, location.x, location.z));
}

bool WaterMapV1::InWater(const glm::vec3& location) const {
	return ReturnRegionType(location) == RegionTypeWater;
}

bool WaterMapV1::InVWater(const glm::vec3& location) const {
	return ReturnRegionType(location) == RegionTypeVWater;
}

bool WaterMapV1::InLava(const glm::vec3& location) const {
	return ReturnRegionType(location) == RegionTypeLava;
}

bool WaterMapV1::InLiquid(const glm::vec3& location) const {
	return InWater(location) || InLava(location);
}

bool WaterMapV1::InPvP(const glm::vec3& location) const {
	return ReturnRegionType(location) == RegionTypePVP;
}

bool WaterMapV1::InZoneLine(const glm::vec3& location) const {
	return ReturnRegionType(location) == RegionTypeZoneLine;
}

bool WaterMapV1::Load(FILE *fp) {
	uint32 bsp_tree_size;
	if (fread(&bsp_tree_size, sizeof(bsp_tree_size), 1, fp) != 1) {
		return false;
	}

	BSP_Root = new ZBSP_Node[bsp_tree_size];
	if (!BSP_Root) {
		return false;
	}

	if (fread(BSP_Root, sizeof(ZBSP_Node), bsp_tree_size, fp) != bsp_tree_size) {
		return false;
	}
	node_count_ = bsp_tree_size;

	return true;
}

WaterRegionType WaterMapV1::BSPReturnRegionType(int32 node_number, const glm::vec3& location) const {
	float distance;

	const ZBSP_Node *current_node = &BSP_Root[node_number - 1];

	if ((current_node->left == 0) &&
		(current_node->right == 0)) {
		return (WaterRegionType)current_node->special;
	}

	distance = (location.x * current_node->normal[0]) +
		(location.y * current_node->normal[1]) +
		(location.z * current_node->normal[2]) +
		current_node->splitdistance;

	if (distance == 0.0f) {
		return(RegionTypeNormal);
	}

	if (distance >0.0f) {
		if (current_node->left == 0) {
			return(RegionTypeNormal);
		}
		return BSPReturnRegionType(current_node->left, location);
	}

	if (current_node->right == 0) {
		return(RegionTypeNormal);
	}

	return BSPReturnRegionType(current_node->right, location);
}

bool WaterMapV1::SegmentInLiquid(const glm::vec3 &from, const glm::vec3 &to) const {
    auto budget = node_count_;
    return SegmentInLiquid(1, {from.y, from.x, from.z}, {to.y, to.x, to.z}, budget, 0);
}
bool WaterMapV1::SegmentInLiquid(int32 number, const glm::vec3 &from, const glm::vec3 &to, uint32 &budget, unsigned depth) const {
    if (!number) { return false; } // Same untagged branch interpretation as the point query.
    if (!budget || depth > 256 || number < 1 || uint32(number) > node_count_) { return true; }
    --budget;
    const auto &node = BSP_Root[number - 1];
    if (!node.left && !node.right) {
        return node.special == RegionTypeWater || node.special == RegionTypeLava || node.special == RegionTypeVWater;
    }
    const auto distance = [&](const glm::vec3 &p) { return p.x*node.normal[0] + p.y*node.normal[1] + p.z*node.normal[2] + node.splitdistance; };
    const auto a = distance(from), b = distance(to);
    if (!std::isfinite(a) || !std::isfinite(b)) { return true; }
    if (a >= 0 && b >= 0 && (a > 0 || b > 0)) { return SegmentInLiquid(node.left, from, to, budget, depth+1); }
    if (a <= 0 && b <= 0 && (a < 0 || b < 0)) { return SegmentInLiquid(node.right, from, to, budget, depth+1); }
    if (a == 0 && b == 0) {
        return SegmentInLiquid(node.left, from, to, budget, depth+1) || SegmentInLiquid(node.right, from, to, budget, depth+1);
    }
    const auto middle = from + (to-from) * (a/(a-b));
    const auto first = a > 0 ? node.left : node.right;
    const auto second = b > 0 ? node.left : node.right;
    return SegmentInLiquid(first, from, middle, budget, depth+1) || SegmentInLiquid(second, middle, to, budget, depth+1);
}
