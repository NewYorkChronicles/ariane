#include <plugin.h>
#include <CPlayerPed.h>
#include <CWorld.h>
#include <cstdio>

#if defined(GTASA)
#include <CGame.h>
#include <CTimeCycle.h>
#include <CStreaming.h>
#endif

using namespace plugin;

class ArianeTeleport {
public:
	ArianeTeleport() {
		Events::gameProcessEvent += [] {
			static bool done = false;
			if(done) return;

			CPlayerPed *player = FindPlayerPed();
			if(!player) return;  // player not spawned yet, retry next frame

			FILE *f = fopen("ariane_teleport.txt", "r");
			if(!f){ done = true; return; }

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
			done = true;
		};
	}
} arianeTeleportInstance;
