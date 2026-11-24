// Real map implementations, independent of ambient zone files. The strip is
// narrower than a navmesh edge and both endpoints are dry.
Result WaterSegmentGeometry()
{
	struct Boxes : WaterMapV2 {
		void Add(WaterRegionType type, glm::vec3 position, glm::vec3 extents, glm::vec3 rotation = glm::vec3(0)) {
			regions.emplace_back(type, OrientedBoundingBox(position, rotation, glm::vec3(1), extents));
		}
	};
	const glm::vec3 from(-10, 0, 0), to(10, 0, 0);
	for (auto type : {RegionTypeWater, RegionTypeLava, RegionTypeVWater}) {
		Boxes map;
		// The existing format exchanges X/Y on every query.
		map.Add(type, glm::vec3(0), {4, 0.001f, 4});
		if (map.InLiquid(from) || map.InLiquid(to) || !map.SegmentInLiquid(from, to) ||
			!map.SegmentInLiquid(to, from) || map.SegmentInLiquid({-10, 8, 0}, {10, 8, 0})) {
			return {Outcome::Failed, "V2 thin liquid strip between dry nodes or dry detour"};
		}
	}
	Boxes carved;
	carved.Add(RegionTypeNormal, glm::vec3(0), {2, 20, 2});
	carved.Add(RegionTypeWater, glm::vec3(0), {4, 0.001f, 4});
	if (carved.SegmentInLiquid(from, to) || !carved.SegmentInLiquid({-10, 3, 0}, {10, 3, 0})) {
		return {Outcome::Failed, "V2 region priority lost dry carve-out"};
	}
	Boxes rotated;
	rotated.Add(RegionTypeLava, glm::vec3(0), {4, 0.001f, 4}, {0, 0, 45});
	if (!rotated.SegmentInLiquid(from, to) || rotated.SegmentInLiquid({-10, 10, 0}, {10, 10, 0})) {
		return {Outcome::Failed, "V2 rotated liquid volume"};
	}
	struct Bsp : WaterMapV1 { using WaterMapV1::Load; } bsp;
	const uint32 count = 5;
	ZBSP_Node nodes[count]{};
	nodes[0].normal[1] = 1; nodes[0].splitdistance = -0.001f; nodes[0].left = 2; nodes[0].right = 3;
	nodes[1].special = RegionTypeNormal;
	nodes[2].normal[1] = 1; nodes[2].splitdistance = 0.001f; nodes[2].left = 4; nodes[2].right = 5;
	nodes[3].special = RegionTypeWater;
	nodes[4].special = RegionTypeNormal;
	auto file = std::unique_ptr<FILE, decltype(&std::fclose)>(std::tmpfile(), &std::fclose);
	if (!file || std::fwrite(&count, sizeof(count), 1, file.get()) != 1 ||
		std::fwrite(nodes, sizeof(ZBSP_Node), count, file.get()) != count) {
		return {Outcome::Failed, "V1 fixture file"};
	}
	std::rewind(file.get());
	if (!bsp.Load(file.get()) || bsp.InLiquid(from) || bsp.InLiquid(to) ||
		!bsp.SegmentInLiquid(from, to) || !bsp.SegmentInLiquid(to, from) ||
		bsp.SegmentInLiquid({-10, 0, 0}, {-1, 0, 0})) {
		return {Outcome::Failed, "V1 BSP thin liquid strip between dry nodes or dry segment"};
	}
	return {Outcome::Passed, "V1/V2 full segments, thin water/lava/vwater, dry priority and rotated volumes"};
}
