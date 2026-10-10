#include <Windows.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include "state/build_data/cache/internal.h"
namespace cache=dawn::state::build_data::cache;
namespace r=cache::records;
int wmain(int argc,wchar_t** argv){if(argc!=2)return 1;std::ifstream file(argv[1],std::ios::binary);r::Header h{};file.read(reinterpret_cast<char*>(&h),sizeof h);if(!file||h.version!=44)return 1;r::MutableDomains d{};r::InvestmentConstants constants{};d.constants=&constants;

std::vector<typename decltype(d.named)::element_type> bank_named(h.namedCount);d.named=bank_named;
std::vector<typename decltype(d.items)::element_type> bank_items(h.itemCount);d.items=bank_items;
std::vector<typename decltype(d.collectibles)::element_type> bank_collectibles(h.collectibleCount);d.collectibles=bank_collectibles;
std::vector<typename decltype(d.materialRequirementSets)::element_type> bank_materialRequirementSets(h.materialRequirementSetCount);d.materialRequirementSets=bank_materialRequirementSets;
std::vector<typename decltype(d.itemDetails)::element_type> bank_itemDetails(h.itemDetailCount);d.itemDetails=bank_itemDetails;
std::vector<typename decltype(d.socketPlugRules)::element_type> bank_socketPlugRules(h.socketPlugRuleCount);d.socketPlugRules=bank_socketPlugRules;
std::vector<typename decltype(d.socketPlugPools)::element_type> bank_socketPlugPools(h.socketPlugPoolCount);d.socketPlugPools=bank_socketPlugPools;
std::vector<typename decltype(d.socketPlugMembers)::element_type> bank_socketPlugMembers(h.socketPlugMemberCount);d.socketPlugMembers=bank_socketPlugMembers;
std::vector<typename decltype(d.inventoryBuckets)::element_type> bank_inventoryBuckets(h.inventoryBucketCount);d.inventoryBuckets=bank_inventoryBuckets;
std::vector<typename decltype(d.socketEntryLists)::element_type> bank_socketEntryLists(h.socketEntryListCount);d.socketEntryLists=bank_socketEntryLists;
std::vector<typename decltype(d.socketEntryTables)::element_type> bank_socketEntryTables(h.socketEntryTableCount);d.socketEntryTables=bank_socketEntryTables;
std::vector<typename decltype(d.abilityBuckets)::element_type> bank_abilityBuckets(h.abilityBucketCount);d.abilityBuckets=bank_abilityBuckets;
std::vector<typename decltype(d.progressions)::element_type> bank_progressions(h.progressionCount);d.progressions=bank_progressions;
std::vector<typename decltype(d.scenarios)::element_type> bank_scenarios(h.scenarioCount);d.scenarios=bank_scenarios;
std::vector<typename decltype(d.rosterGroups)::element_type> bank_rosterGroups(h.rosterGroupCount);d.rosterGroups=bank_rosterGroups;
std::vector<typename decltype(d.spawnStems)::element_type> bank_spawnStems(h.spawnStemCount);d.spawnStems=bank_spawnStems;
std::vector<typename decltype(d.spawnNameHashes)::element_type> bank_spawnNameHashes(h.spawnNameHashCount);d.spawnNameHashes=bank_spawnNameHashes;
std::vector<typename decltype(d.spawnPoints)::element_type> bank_spawnPoints(h.spawnPointCount);d.spawnPoints=bank_spawnPoints;
std::vector<typename decltype(d.hashNames)::element_type> bank_hashNames(h.hashNameCount);d.hashNames=bank_hashNames;
std::vector<typename decltype(d.vendorIndex)::element_type> bank_vendorIndex(h.vendorIndexCount);d.vendorIndex=bank_vendorIndex;
std::vector<typename decltype(d.vendorDefinitions)::element_type> bank_vendorDefinitions(h.vendorDefinitionCount);d.vendorDefinitions=bank_vendorDefinitions;
std::vector<typename decltype(d.vendorSaleRows)::element_type> bank_vendorSaleRows(h.vendorSaleRowCount);d.vendorSaleRows=bank_vendorSaleRows;
std::vector<typename decltype(d.vendorInstalledRows)::element_type> bank_vendorInstalledRows(h.vendorInstalledRowCount);d.vendorInstalledRows=bank_vendorInstalledRows;
r::DomainCounts counts{};const dawn::state::build_data::BuildIdentity expected{h.imageTimestamp,h.imageSize,h.configuredEquipmentHash};auto result=cache::load(argv[1],expected,d,counts);if(result!=cache::LoadStatus::loaded){std::printf("FAIL native cache loader status=%u\n",static_cast<unsigned>(result));return 1;}if(constants.weaponPowerStatRow!=13)return 1;std::printf("PASS native v44 loader weapon_power_row=%u scenarios=%zu roster_groups=%zu\n",constants.weaponPowerStatRow,counts.scenarios,counts.rosterGroups);return 0;}
