// Included inside the bridge-enabled in-server registry. Temporary tables are
// connection-local; the retained physical schema and live spawn rows are untouched.
Result ActorSpawnHints()
{
	// Production may alias its two globals to one MySQL handle. Separate owned
	// connections make contradictory TEMPORARY fixtures safe in either config.
	const auto *config = EQEmuConfig::get();
	Database content_connection, runtime_connection;
	const bool separate_content = !config->ContentDbHost.empty();
	if (!runtime_connection.Connect(config->DatabaseHost.c_str(), config->DatabaseUsername.c_str(),
		config->DatabasePassword.c_str(), config->DatabaseDB.c_str(), config->DatabasePort) ||
		!content_connection.Connect(
			(separate_content ? config->ContentDbHost : config->DatabaseHost).c_str(),
			(separate_content ? config->ContentDbUsername : config->DatabaseUsername).c_str(),
			(separate_content ? config->ContentDbPassword : config->DatabasePassword).c_str(),
			(separate_content ? config->ContentDbName : config->DatabaseDB).c_str(),
			separate_content ? config->ContentDbPort : config->DatabasePort)) {
		return {Outcome::Failed, "spawn hint test-owned connections unavailable"};
	}
	struct Tables {
		Database &db;
		std::vector<std::string> names;
		~Tables() {
			for (const auto &name : names) { db.QueryDatabase("DROP TEMPORARY TABLE IF EXISTS " + name); }
		}
		bool Create(const std::string &name, const std::string &definition) {
			if (!db.QueryDatabase("CREATE TEMPORARY TABLE " + name + definition).Success()) { return false; }
			names.push_back(name);
			return true;
		}
		bool Replace(const std::string &name, const std::string &definition) {
			return db.QueryDatabase("DROP TEMPORARY TABLE " + name).Success() &&
				db.QueryDatabase("CREATE TEMPORARY TABLE " + name + definition).Success();
		}
	} content{content_connection, {}}, runtime{runtime_connection, {}};
	auto fail = [](const char *why) { return Result{Outcome::Failed, why}; };
	// Clone the actual retained spawn2 schema: it must have no enabled column.
	if (!content.Create("spawn2", " LIKE spawn2") ||
		!content.Create("spawnentry", " LIKE spawnentry") ||
		!content.Create("npc_types", " (id INT PRIMARY KEY,level INT,npc_faction_id INT,merchant_id INT)") ||
		!content.Create("npc_faction", " (id INT PRIMARY KEY,primaryfaction INT)") ||
		!content.Create("spawn2_disabled", " (spawn2_id INT,instance_id INT,disabled INT)") ||
		!runtime.Create("spawn2_disabled", " (spawn2_id INT,instance_id INT,disabled INT)")) {
		return fail("spawn hint temporary schema creation");
	}
	auto columns = content_connection.QueryDatabase("SHOW COLUMNS FROM spawn2 LIKE 'enabled'");
	if (!columns.Success() || columns.RowCount() != 0) { return fail("retained spawn2 schema unexpectedly contains enabled"); }
	if (!content_connection.QueryDatabase("INSERT INTO npc_faction VALUES (100,306)").Success() ||
		!content_connection.QueryDatabase("INSERT INTO npc_types VALUES (100,2,100,0),(101,9,100,0),(102,2,100,1),(103,0,100,0),(104,4,100,0),(105,1,100,0)").Success() ||
		!content_connection.QueryDatabase("INSERT INTO spawn2_disabled VALUES (1,0,1),(3,0,0)").Success() ||
		!runtime_connection.QueryDatabase("INSERT INTO spawn2_disabled VALUES (2,0,0),(3,0,1),(4,0,-1),(6,77,1)").Success()) {
		return fail("spawn hint fixture insertion");
	}
	const std::string flag = "central_cdau_never_enabled_8df5d0c8";
	const auto enabled_flags = WorldContentService::Instance()->GetContentFlagsEnabled();
	if (std::find(enabled_flags.begin(), enabled_flags.end(), flag) != enabled_flags.end()) {
		return fail("spawn hint exclusion flag unexpectedly enabled");
	}
	for (int id = 1; id <= 16; ++id) {
		const auto version = id == 5 ? -1 : id == 7 ? 1 : 0;
		const auto npc = id == 8 ? 101 : id == 9 ? 102 : id == 14 ? 103 : id == 15 ? 104 : id == 16 ? 105 : 100;
		const auto spawn_flag = id == 10 ? flag : "";
		const auto entry_flag = id == 11 ? flag : "";
		if (!content_connection.QueryDatabase(fmt::format(
			"INSERT INTO spawn2 (id,spawngroupID,zone,version,x,y,z,min_expansion,max_expansion,content_flags) "
			"VALUES ({},{},'central_cdau_fixture',{},1,2,0,{},-1,'{}')",
			id, id, version, id == 12 ? 127 : -1, spawn_flag)).Success() ||
			!content_connection.QueryDatabase(fmt::format(
			"INSERT INTO spawnentry (spawngroupID,npcID,chance,min_expansion,max_expansion,content_flags) "
			"VALUES ({},{},100,{},-1,'{}')", id, npc, id == 13 ? 127 : -1, entry_flag)).Success()) {
			return fail("spawn hint content row insertion");
		}
	}
	auto hints = ActorZones::ReadSpawnHints(content_connection, runtime_connection, "central_cdau_fixture", {1,4});
	if (!hints) { return fail("retained-schema spawn hint projection failed"); }
	std::vector<uint32_t> ids;
	for (const auto &hint : *hints) {
		ids.push_back(hint.spawn_id);
		const auto expected_level = hint.spawn_id == 15 ? 4 : hint.spawn_id == 16 ? 1 : 2;
		if (hint.level != expected_level || hint.faction != 306 || hint.x != 1 || hint.y != 2) { return fail("spawn hint facts changed"); }
	}
	if (ids != std::vector<uint32_t>{1,2,5,6,15,16}) {
		return fail("spawn hint runtime override, instance-zero, version, level, merchant or content filter");
	}
	auto empty = ActorZones::ReadSpawnHints(content_connection, runtime_connection, "central_cdau_absent", {1,4});
	if (!empty || !empty->empty()) {
		return fail("empty spawn hints are not distinct from query failure");
	}
	// Check the production bound with real rows, not a mirrored SQL string.
	std::string values;
	for (unsigned id = 20000; id < 24097; ++id) {
		if (!values.empty()) { values += ','; }
		values += fmt::format("({},1,'central_cdau_fixture',0,1,2,0,-1,-1)", id);
	}
	if (!content_connection.QueryDatabase("INSERT INTO spawn2 (id,spawngroupID,zone,version,x,y,z,min_expansion,max_expansion) VALUES " + values).Success() ||
		ActorZones::ReadSpawnHints(content_connection, runtime_connection, "central_cdau_fixture", {1,4}).has_value()) {
		return fail("spawn hint candidate bound failed open");
	}
	if (!content_connection.QueryDatabase("DELETE FROM spawn2 WHERE id>=20000").Success()) { return fail("spawn hint bound fixture cleanup"); }
	// Intentional malformed TEMPORARY schemas exercise actual query failures.
	if (!runtime.Replace("spawn2_disabled", " (unexpected_column INT)") || ActorZones::ReadSpawnHints(content_connection, runtime_connection, "central_cdau_fixture", {1,4}).has_value()) {
		return fail("runtime override query failure treated as enabled");
	}
	if (!runtime.Replace("spawn2_disabled", " (spawn2_id INT,instance_id INT,disabled INT)") ||
		!content.Replace("npc_types", " (unexpected_column INT)") || ActorZones::ReadSpawnHints(content_connection, runtime_connection, "central_cdau_fixture", {1,4}).has_value()) {
		return fail("content query failure treated as empty success");
	}
	return {Outcome::Passed, "retained spawn2 schema, separate runtime overrides, restrictions, bounds and fail-closed errors"};
}
