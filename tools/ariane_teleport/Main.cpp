#include <plugin.h>
#include <CPlayerPed.h>
#include <CWorld.h>
#include <cstdio>
#include <cstring>
#include <cmath>

#if defined(GTASA)
#include <CGame.h>
#include <CTimeCycle.h>
#include <CColStore.h>
#include <CFileLoader.h>
#include <CFileObjectInstance.h>
#include <CStreaming.h>
#include <CIplStore.h>
#include <CEntity.h>
#include <CMatrix.h>
#endif

using namespace plugin;

#if defined(GTASA)
// Find a building/dummy entity by model ID near a position.
// Returns the closest match within radius, or NULL.
static CEntity*
FindEntityByModelNearPos(int modelId, float x, float y, float z, float radius)
{
	CEntity *found = NULL;
	float bestDist = radius * radius;

	const auto ConsiderMatches = [&](CEntity **entities, short count) {
		for(int i = 0; i < count; i++){
			if(entities[i]->m_nModelIndex != modelId)
				continue;
			CVector &epos = entities[i]->GetPosition();
			float dx = epos.x - x;
			float dy = epos.y - y;
			float dz = epos.z - z;
			float dist = dx*dx + dy*dy + dz*dz;
			if(dist < bestDist){
				bestDist = dist;
				found = entities[i];
			}
		}
	};

	short count = 0;
	CEntity *entities[512];
	CVector pos(x, y, z);

	CWorld::FindObjectsInRange(pos, radius, true, &count, 512, entities,
		true,   // buildings
		false,  // vehicles
		false,  // peds
		false,  // objects
		true);  // dummies
	ConsiderMatches(entities, count);

	short lodCount = 0;
	CEntity *lodEntities[512];
	CWorld::FindLodOfTypeInRange(modelId, pos, radius, true, &lodCount, 512, lodEntities);
	ConsiderMatches(lodEntities, lodCount);
	return found;
}

static void
PromoteToBigBuildingIfNeeded(CEntity *entity)
{
	if(!entity)
		return;
	const CBaseModelInfo *mi = CModelInfo::GetModelInfo(entity->m_nModelIndex);
	if(!mi)
		return;
	if(entity->m_nNumLodChildren == 0 && mi->m_fDrawDistance <= 300.0f)
		return;
	if(entity->bIsBIGBuilding)
		return;
	CWorld::Remove(entity);
	entity->SetupBigBuilding();
	CWorld::Add(entity);
}

// Process entity-level commands from ariane_reload_entities.txt
static void
ProcessEntityReload(void)
{
	FILE *f = fopen("ariane_reload_entities.txt", "r");
	if(!f) return;

	struct PendingLodLink {
		CEntity *entity;
		int lodModelId;
		float lodX, lodY, lodZ;
	};
	PendingLodLink pendingLodLinks[256];
	int numPendingLodLinks = 0;

	char line[256];
	while(fgets(line, sizeof(line), f)){
		if(line[0] == 'A'){
			// A modelId x y z qx qy qz qw area lodModelId lodX lodY lodZ
			int modelId, area, lodModelId;
			float x, y, z, qx, qy, qz, qw, lodX, lodY, lodZ;
			if(sscanf(line + 2, "%d %f %f %f %f %f %f %f %d %d %f %f %f",
				&modelId, &x, &y, &z, &qx, &qy, &qz, &qw,
				&area, &lodModelId, &lodX, &lodY, &lodZ) == 13)
			{
				CFileObjectInstance inst = {};
				inst.m_nModelId = modelId;
				inst.m_vecPosition = CVector(x, y, z);
				inst.m_qRotation.imag = CVector(qx, qy, qz);
				inst.m_qRotation.real = qw;
				inst.m_nInstanceType = 0;
				inst.m_nAreaCode = area & 0xFF;
				inst.m_bRedundantStream = (area & 0x100) != 0;
				inst.m_bUnderwater = (area & 0x400) != 0;
				inst.m_bTunnel = (area & 0x800) != 0;
				inst.m_bTunnelTransition = (area & 0x1000) != 0;
				inst.m_nLodInstanceIndex = -1;

				CEntity *e = CFileLoader::LoadObjectInstance(&inst, NULL);
				if(e){
					CWorld::Add(e);
					PromoteToBigBuildingIfNeeded(e);
					e->UpdateRwFrame();

					if(lodModelId >= 0 && numPendingLodLinks < 256){
						pendingLodLinks[numPendingLodLinks].entity = e;
						pendingLodLinks[numPendingLodLinks].lodModelId = lodModelId;
						pendingLodLinks[numPendingLodLinks].lodX = lodX;
						pendingLodLinks[numPendingLodLinks].lodY = lodY;
						pendingLodLinks[numPendingLodLinks].lodZ = lodZ;
						numPendingLodLinks++;
					}
				}
			}
		}else if(line[0] == 'M'){
			// M modelId oldX oldY oldZ newX newY newZ qx qy qz qw
			int modelId;
			float ox, oy, oz, nx, ny, nz, qx, qy, qz, qw;
			if(sscanf(line + 2, "%d %f %f %f %f %f %f %f %f %f %f",
				&modelId, &ox, &oy, &oz, &nx, &ny, &nz,
				&qx, &qy, &qz, &qw) == 11)
			{
				CEntity *e = FindEntityByModelNearPos(modelId, ox, oy, oz, 50.0f);
				if(e){
					CWorld::Remove(e);
					e->SetPosn(nx, ny, nz);

					// Build rotation matrix from quaternion
					CMatrix mat;
					float x2 = qx+qx, y2 = qy+qy, z2 = qz+qz;
					float xx = qx*x2, xy = qx*y2, xz = qx*z2;
					float yy = qy*y2, yz = qy*z2, zz = qz*z2;
					float wx = qw*x2, wy = qw*y2, wz = qw*z2;

					mat.right.x = 1.0f-(yy+zz); mat.right.y = xy+wz;         mat.right.z = xz-wy;
					mat.up.x    = xy-wz;         mat.up.y    = 1.0f-(xx+zz); mat.up.z    = yz+wx;
					mat.at.x    = xz+wy;         mat.at.y    = yz-wx;         mat.at.z    = 1.0f-(xx+yy);
					mat.pos.x   = nx;            mat.pos.y   = ny;            mat.pos.z   = nz;

					e->SetMatrix(mat);
					CWorld::Add(e);
					e->UpdateRwFrame();
				}
			}
		}else if(line[0] == 'D'){
			// D modelId oldX oldY oldZ
			int modelId;
			float ox, oy, oz;
			if(sscanf(line + 2, "%d %f %f %f", &modelId, &ox, &oy, &oz) == 4){
				CEntity *e = FindEntityByModelNearPos(modelId, ox, oy, oz, 50.0f);
				if(e){
					CWorld::Remove(e);
					delete e;
				}
			}
		}
	}

	for(int i = 0; i < numPendingLodLinks; i++){
		PendingLodLink &link = pendingLodLinks[i];
		CEntity *lod = FindEntityByModelNearPos(link.lodModelId, link.lodX, link.lodY, link.lodZ, 50.0f);
		if(!lod || !link.entity)
			continue;
		link.entity->m_pLod = lod;
		lod->m_nNumLodChildren++;
		PromoteToBigBuildingIfNeeded(lod);
		PromoteToBigBuildingIfNeeded(link.entity);
	}

	fclose(f);
	remove("ariane_reload_entities.txt");
}
#endif

class ArianeTeleport {
public:
	ArianeTeleport() {
		Events::gameProcessEvent += [] {
			CPlayerPed *player = FindPlayerPed();
			if(!player) return;

			// --- Phase 1: one-time teleport from editor ---
			static bool teleportDone = false;
			if(!teleportDone){
				FILE *f = fopen("ariane_teleport.txt", "r");
				if(!f){ teleportDone = true; }
				else {
					float x, y, z, heading;
					int area = 0;
					int n = fscanf(f, "%f %f %f %f %d", &x, &y, &z, &heading, &area);
					fclose(f);
					remove("ariane_teleport.txt");

					if(n >= 4){
#if defined(GTASA)
						CGame::currArea = area;
						player->m_nAreaCode = area;
						player->Teleport(CVector(x, y, z), false);
						CVector loadPos(x, y, z);
						CStreaming::LoadScene(&loadPos);
						CTimeCycle::StopExtraColour(false);
#else
						player->Teleport(CVector(x, y, z));
#endif
						player->SetHeading(heading);
					}
					teleportDone = true;
				}
			}

#if defined(GTASA)
			// --- Phase 2: hot reload streaming IPLs ---
			{
				FILE *f = fopen("ariane_reload.txt", "r");
				if(f){
					char line[64];
					int slots[256];
					int numSlots = 0;

					while(fgets(line, sizeof(line), f) && numSlots < 256){
						char *end = line + strlen(line) - 1;
						while(end >= line && (*end == '\n' || *end == '\r' || *end == ' '))
							*end-- = '\0';
						if(line[0] == '\0') continue;

						int slot = CIplStore::FindIplSlot(line);
						if(slot >= 0)
							slots[numSlots++] = slot;
					}
					fclose(f);
					remove("ariane_reload.txt");

					if(numSlots > 0){
						CVector reloadPos = player->GetPosition();
						for(int i = 0; i < numSlots; i++)
							CIplStore::RemoveIplAndIgnore(slots[i]);
						for(int i = 0; i < numSlots; i++)
							CIplStore::RequestIplAndIgnore(slots[i]);
						CStreaming::LoadAllRequestedModels(false);
						CColStore::EnsureCollisionIsInMemory(reloadPos);
					}
				}
			}

			// --- Phase 3: hot reload text IPL entities ---
			ProcessEntityReload();
#endif
		};
	}
} arianeTeleportInstance;
