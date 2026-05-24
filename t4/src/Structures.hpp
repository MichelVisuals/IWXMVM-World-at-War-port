#pragma once

namespace IWXMVM::T4::Structures
{

    struct cmd_function_t
    {
        cmd_function_t* next;
        char* name;
        char* autocomplete1;
        char* autocomplete2;
        void* function;
    };

    enum netadrtype_t
    {
        NA_BOT = 0x0,
        NA_BAD = 0x1,
        NA_LOOPBACK = 0x2,
        NA_BROADCAST = 0x3,
        NA_IP = 0x4,
        NA_IPX = 0x5,
        NA_BROADCAST_IPX = 0x6,
    };

    struct netadr_t
    {
        netadrtype_t type;
        char ip[4];
        unsigned __int16 port;
        char ipx[10];
    };

    enum netsrc_t
    {
        NS_CLIENT1 = 0x0,
        NS_SERVER = 0x1,
        NS_MAXCLIENTS = 0x1,
        NS_PACKET = 0x2,
    };

    struct netProfilePacket_t
    {
        int iTime;
        int iSize;
        int bFragment;
    };

    struct netProfileStream_t
    {
        netProfilePacket_t packets[60];
        int iCurrPacket;
        int iBytesPerSecond;
        int iLastBPSCalcTime;
        int iCountedPackets;
        int iCountedFragments;
        int iFragmentPercentage;
        int iLargestPacket;
        int iSmallestPacket;
    };

    struct netProfileInfo_t
    {
        netProfileStream_t send;
        netProfileStream_t recieve;
    };

    struct netchan_t
    {
        int outgoingSequence;
        netsrc_t sock;
        int dropped;
        int incomingSequence;
        netadr_t remoteAddress;
        int qport;
        int fragmentSequence;
        int fragmentLength;
        char* fragmentBuffer;
        int fragmentBufferSize;
        int unsentFragments;
        int unsentFragmentStart;
        int unsentLength;
        char* unsentBuffer;
        int unsentBufferSize;
        netProfileInfo_t prof;
    };

    // T4 (CoDWaWmp.exe) clientStatic_t — derived 2026-05-23 session 4.
    // Caball009 gives us cls.realtime at VA 0xBD3628 + cls.realFrametime at
    // 0xBD362C. In the IW3 clientStatic_t layout, realtime is at offset
    // 0x118 with realFrametime immediately after at 0x11C. So:
    //   base = 0xBD3628 - 0x118 = 0xBD3510
    // The first 0x118 bytes (quit / hunkUsersStarted / servername / etc.)
    // likely follow IW3 layout, but to be safe we use a byte-pad prefix
    // and only expose the verified fields. static_asserts pin the offsets.
    struct clientStatic_t
    {
        byte _t4_pad_prefix[0x118];          // 0x000..0x117  prefix (servername etc.)
        int realtime;                        // 0x118 ✅
        int realFrametime;                   // 0x11C ✅
        // Beyond this point, fields are at unverified T4 offsets. Rewind
        // state machine only reads/writes realtime (and indirectly via
        // PlaybackData address pointer), so leaving the tail opaque is
        // safe for now.
        byte _t4_pad_tail[0x500];            // tail slack
    };
    static_assert(offsetof(clientStatic_t, realtime) == 0x118,
                  "T4 clientStatic_t.realtime must be at +0x118 (so 0xBD3628 from base 0xBD3510)");
    static_assert(offsetof(clientStatic_t, realFrametime) == 0x11C,
                  "T4 clientStatic_t.realFrametime must be at +0x11C");

    struct clientConnection_t
    {
        int qport;
        int clientNum;
        int lastPacketSentTime;
        int lastPacketTime;
        netadr_t serverAddress;
        int connectTime;
        int connectPacketCount;
        char serverMessage[256];
        int challenge;
        int checksumFeed;
        int reliableSequence;
        int reliableAcknowledge;
        char reliableCommands[128][1024];
        int serverMessageSequence;
        int serverCommandSequence;
        int lastExecutedServerCommand;
        char serverCommands[128][1024];
        bool isServerRestarting;
        int lastClientArchiveIndex;
        int _t4_pad_before_demoName;  // T4: demoName + downstream demo fields are
                                       // shifted +4 bytes vs IW3. Empirically located
                                       // by scanning clc memory for the loaded demo's
                                       // basename and matching field-value patterns
                                       // (demorecording=0, demoplaying=1, demofile=1).
        char demoName[64];
        int demorecording;
        int demoplaying;
        int isTimeDemo;
        int demowaiting;
        int firstDemoFrameSkipped;
        int demofile;
        int timeDemoLog;
        int timeDemoFrames;
        int timeDemoStart;
        int timeDemoPrev;
        int timeDemoBaseTime;
        netchan_t netchan;
        char netchanOutgoingBuffer[2048];
        char netchanIncomingBuffer[131072];
        netProfileInfo_t OOBProf;
        char statPacketsToSend;
        int statPacketSendTime[7];
    };

    enum OffhandSecondaryClass
    {
        PLAYER_OFFHAND_SECONDARY_SMOKE = 0x0,
        PLAYER_OFFHAND_SECONDARY_FLASH = 0x1,
        PLAYER_OFFHAND_SECONDARIES_TOTAL = 0x2,
    };

    enum ViewLockTypes
    {
        PLAYERVIEWLOCK_NONE = 0x0,
        PLAYERVIEWLOCK_FULL = 0x1,
        PLAYERVIEWLOCK_WEAPONJITTER = 0x2,
        PLAYERVIEWLOCKCOUNT = 0x3,
    };

    struct SprintState
    {
        int sprintButtonUpRequired;
        int sprintDelay;
        int lastSprintStart;
        int lastSprintEnd;
        int sprintStartMaxLength;
    };

    struct MantleState
    {
        float yaw;
        int timer;
        int transIndex;
        int flags;
    };

    enum ActionSlotType
    {
        ACTIONSLOTTYPE_DONOTHING = 0x0,
        ACTIONSLOTTYPE_SPECIFYWEAPON = 0x1,
        ACTIONSLOTTYPE_ALTWEAPONTOGGLE = 0x2,
        ACTIONSLOTTYPE_NIGHTVISION = 0x3,
        ACTIONSLOTTYPECOUNT = 0x4,
    };

    struct ActionSlotParam_SpecifyWeapon
    {
        unsigned int index;
    };

    /* 120 */
    struct ActionSlotParam
    {
        ActionSlotParam_SpecifyWeapon specifyWeapon;
    };

    enum objectiveState_t
    {
        OBJST_EMPTY = 0x0,
        OBJST_ACTIVE = 0x1,
        OBJST_INVISIBLE = 0x2,
        OBJST_DONE = 0x3,
        OBJST_CURRENT = 0x4,
        OBJST_FAILED = 0x5,
        OBJST_NUMSTATES = 0x6,
    };

    struct objective_t
    {
        objectiveState_t state;
        float origin[3];
        int entNum;
        int teamNum;
        int icon;
    };

    struct hudelem_s
    {
        std::byte content[0xA0];
    };

    struct hudelements_s
    {
        hudelem_s current[31];
        hudelem_s archival[31];
    };

    struct playerState_s
    {
        int commandTime;
        int pm_type;
        int bobCycle;
        int pm_flags;
        int weapFlags;
        int otherFlags;
        int pm_time;
        float origin[3];
        float velocity[3];
        float oldVelocity[2];
        int weaponTime;
        int weaponDelay;
        int grenadeTimeLeft;
        int throwBackGrenadeOwner;
        int throwBackGrenadeTimeLeft;
        int weaponRestrictKickTime;
        int foliageSoundTime;
        int gravity;
        float leanf;
        int speed;
        float delta_angles[3];
        int groundEntityNum;
        float vLadderVec[3];
        int jumpTime;
        float jumpOriginZ;
        int legsTimer;
        int legsAnim;
        int torsoTimer;
        int torsoAnim;
        int legsAnimDuration;
        int torsoAnimDuration;
        int damageTimer;
        int damageDuration;
        int flinchYawAnim;
        int movementDir;
        int eFlags;
        int eventSequence;
        int events[4];
        unsigned int eventParms[4];
        int oldEventSequence;
        int clientNum;
        int offHandIndex;
        OffhandSecondaryClass offhandSecondary;
        unsigned int weapon;
        int weaponstate;
        unsigned int weaponShotCount;
        float fWeaponPosFrac;
        int adsDelayTime;
        int spreadOverride;
        int spreadOverrideState;
        int viewmodelIndex;
        float viewangles[3];
        int viewHeightTarget;
        float viewHeightCurrent;
        int viewHeightLerpTime;
        int viewHeightLerpTarget;
        int viewHeightLerpDown;
        float viewAngleClampBase[2];
        float viewAngleClampRange[2];
        int damageEvent;
        int damageYaw;
        int damagePitch;
        int damageCount;
        int stats[5];
        int ammo[128];
        int ammoclip[128];
        unsigned int weapons[4];
        unsigned int weaponold[4];
        unsigned int weaponrechamber[4];
        float proneDirection;
        float proneDirectionPitch;
        float proneTorsoPitch;
        ViewLockTypes viewlocked;
        int viewlocked_entNum;
        int cursorHint;
        int cursorHintString;
        int cursorHintEntIndex;
        int iCompassPlayerInfo;
        int radarEnabled;
        int locationSelectionInfo;
        SprintState sprintState;
        float fTorsoPitch;
        float fWaistPitch;
        float holdBreathScale;
        int holdBreathTimer;
        float moveSpeedScaleMultiplier;
        MantleState mantleState;
        float meleeChargeYaw;
        int meleeChargeDist;
        int meleeChargeTime;
        int perks;
        ActionSlotType actionSlotType[4];
        ActionSlotParam actionSlotParam[4];
        int entityEventSequence;
        int weapAnim;
        float aimSpreadScale;
        int shellshockIndex;
        int shellshockTime;
        int shellshockDuration;
        float dofNearStart;
        float dofNearEnd;
        float dofFarStart;
        float dofFarEnd;
        float dofNearBlur;
        float dofFarBlur;
        float dofViewmodelStart;
        float dofViewmodelEnd;
        int hudElemLastAssignedSoundID;
        objective_t objective[16];
        char weaponmodels[128];
        int deltaTime;
        int killCamEntity;
        hudelements_s hud;
    };

    // T4 (CoDWaWmp.exe) layout — verified 2026-05-23 via EAX-capture probe
    // against live refdef at 0x009E676C. Differs from IW3 by an inserted
    // 4-byte field between tanHalfFovY (+0x14) and vieworg (which sits at
    // +0x1C in T4, not IW3's +0x18). The stored_fov field reads a constant
    // 65.0 during playback — almost certainly a cached cg_fov value in
    // degrees, refreshed each frame by R_SetViewParmsForScene.
    //
    // All offsets confirmed in IWXMVM.log:
    //   +0x00 x/y/w/h = 0/0/2560/1440 (live screen res)
    //   +0x10 tanHalfFovX = 0.8494 (≈ atan 40.4° half = 80.8° full)
    //   +0x14 tanHalfFovY = 0.4778
    //   +0x18 stored_fov  = 65.00 constant
    //   +0x1C vieworg     = (-9266, -16957, 130) → moves smoothly w/ player
    //   +0x2C viewaxis    (per Ghidra; not yet probe-confirmed in T4 MP — 4-byte
    //                       gap at +0x28 may be pad or a 4th vieworg float)
    struct refdef_s
    {
        unsigned int x;          // 0x00
        unsigned int y;          // 0x04
        unsigned int width;      // 0x08
        unsigned int height;     // 0x0C
        float tanHalfFovX;       // 0x10
        float tanHalfFovY;       // 0x14
        float stored_fov;        // 0x18  — T4-specific (cg_fov in degrees)
        float vieworg[3];        // 0x1C
        float _t4_pad28;         // 0x28  — vec3-to-vec4 alignment or pad; verify
        float viewaxis[3][3];    // 0x2C  — per Ghidra
        float viewOffset[3];     // 0x50
        int time;                // 0x5C
        float zNear;             // 0x60
        float blurRadius;        // 0x64
        byte filler[0x4030];     // pad to original IW3 sizeof
    };
    static_assert(offsetof(refdef_s, vieworg) == 0x1C,
                  "T4 refdef_s.vieworg must be at offset 0x1C — verified via R_SetViewParmsForScene EAX capture 2026-05-23");
    static_assert(offsetof(refdef_s, viewaxis) == 0x2C,
                  "T4 refdef_s.viewaxis must be at offset 0x2C — per Ghidra R_SetViewParmsForScene disassembly");

    // T4 (CoDWaWmp.exe) layout — total sizeof MUST be 0x39E0 (14816) per
    // Caball009's working MP demo-rewind PoC (32 of these live at
    // clientActive_s+0x6B33C, spaced exactly 0x39E0 apart). The first three
    // fields (valid, snapFlags, serverTime) are at standard CoD-engine
    // offsets 0x00/0x04/0x08; the rest of the struct (playerState_s,
    // numEntities, parseEntitiesNum, etc.) has unverified T4 offsets so
    // we represent it as an opaque buffer. The Rewinding state machine
    // only memcpys the snapshot ring whole, so field-level access past
    // serverTime isn't needed.
    struct clSnapshot_t
    {
        int valid;       // 0x00
        int snapFlags;   // 0x04
        int serverTime;  // 0x08
        byte _t4_opaque[0x39E0 - 0xC];  // 0x0C..0x39DF
    };
    static_assert(sizeof(clSnapshot_t) == 0x39E0,
                  "T4 clSnapshot_t must be exactly 0x39E0 (14816) bytes per Caball009");

    struct gameState_t
    {
        int stringOffsets[2442];
        char stringData[131072];
        int dataCount;
    };

    enum StanceState
    {
        CL_STANCE_STAND = 0x0,
        CL_STANCE_CROUCH = 0x1,
        CL_STANCE_PRONE = 0x2,
    };

    struct usercmd_s
    {
        int serverTime;
        int buttons;
        int angles[3];
        char weapon;
        char offHandIndex;
        char forwardmove;
        char rightmove;
        float meleeChargeYaw;
        char meleeChargeDist;
        char selectedLocation[2];
    };

    struct ClientArchiveData
    {
        int serverTime;
        float origin[3];
        float velocity[3];
        int bobCycle;
        int movementDir;
        float viewangles[3];
    };

    struct outPacket_t
    {
        int p_cmdNumber;
        int p_serverTime;
        int p_realtime;
    };

    enum entityType_t
    {
        ET_GENERAL = 0x0,
        ET_PLAYER = 0x1,
        ET_PLAYER_CORPSE = 0x2,
        ET_ITEM = 0x3,
        ET_MISSILE = 0x4,
        ET_INVISIBLE = 0x5,
        ET_SCRIPTMOVER = 0x6,
        ET_SOUND_BLEND = 0x7,
        ET_FX = 0x8,
        ET_LOOP_FX = 0x9,
        ET_PRIMARY_LIGHT = 0xA,
        ET_MG42 = 0xB,
        ET_HELICOPTER = 0xC,
        ET_PLANE = 0xD,
        ET_VEHICLE = 0xE,
        ET_VEHICLE_COLLMAP = 0xF,
        ET_VEHICLE_CORPSE = 0x10,
        ET_EVENTS = 0x11,
    };

    /* 255 */
    enum trType_t
    {
        TR_STATIONARY = 0x0,
        TR_INTERPOLATE = 0x1,
        TR_LINEAR = 0x2,
        TR_LINEAR_STOP = 0x3,
        TR_SINE = 0x4,
        TR_GRAVITY = 0x5,
        TR_ACCELERATE = 0x6,
        TR_DECELERATE = 0x7,
        TR_PHYSICS = 0x8,
        TR_FIRST_RAGDOLL = 0x9,
        TR_RAGDOLL = 0x9,
        TR_RAGDOLL_GRAVITY = 0xA,
        TR_RAGDOLL_INTERPOLATE = 0xB,
        TR_LAST_RAGDOLL = 0xB,
    };

    /* 723 */
    struct trajectory_t
    {
        trType_t trType;
        int trTime;
        int trDuration;
        float trBase[3];
        float trDelta[3];
    };

    /* 1003 */
    struct LerpEntityStateTurret
    {
        float gunAngles[3];
    };

    /* 1001 */
    struct LerpEntityStateLoopFx
    {
        float cullDist;
        int period;
    };

    /* 1009 */
    struct LerpEntityStatePrimaryLight
    {
        char colorAndExp[4];
        float intensity;
        float radius;
        float cosHalfFovOuter;
        float cosHalfFovInner;
    };

    /* 931 */
    struct LerpEntityStatePlayer
    {
        float leanf;
        int movementDir;
    };

    /* 926 */
    struct LerpEntityStateVehicle
    {
        float bodyPitch;
        float bodyRoll;
        float steerYaw;
        int materialTime;
        float gunPitch;
        float gunYaw;
        int teamAndOwnerIndex;
    };

    /* 1011 */
    struct LerpEntityStateMissile
    {
        int launchTime;
    };

    /* 1012 */
    struct LerpEntityStateSoundBlend
    {
        float lerp;
    };

    /* 1008 */
    struct LerpEntityStateBulletHit
    {
        float start[3];
    };

    /* 924 */
    struct LerpEntityStateEarthquake
    {
        float scale;
        float radius;
        int duration;
    };

    /* 1002 */
    struct LerpEntityStateCustomExplode
    {
        int startTime;
    };

    /* 1007 */
    struct LerpEntityStateExplosion
    {
        float innerRadius;
        float magnitude;
    };

    /* 1019 */
    struct LerpEntityStateExplosionJolt
    {
        float innerRadius;
        float impulse[3];
    };

    /* 929 */
    struct LerpEntityStatePhysicsJitter
    {
        float innerRadius;
        float minDisplacement;
        float maxDisplacement;
    };

    /* 1004 */
    struct LerpEntityStateAnonymous
    {
        int data[7];
    };

    /* 1133 */
    union LerpEntityStateTypeUnion
    {
        LerpEntityStateTurret turret;
        LerpEntityStateLoopFx loopFx;
        LerpEntityStatePrimaryLight primaryLight;
        LerpEntityStatePlayer player;
        LerpEntityStateVehicle vehicle;
        LerpEntityStateMissile missile;
        LerpEntityStateSoundBlend soundBlend;
        LerpEntityStateBulletHit bulletHit;
        LerpEntityStateEarthquake earthquake;
        LerpEntityStateCustomExplode customExplode;
        LerpEntityStateExplosion explosion;
        LerpEntityStateExplosionJolt explosionJolt;
        LerpEntityStatePhysicsJitter physicsJitter;
        LerpEntityStateAnonymous anonymous;
    };

    /* 1140 */
    struct LerpEntityState
    {
        int eFlags;
        trajectory_t pos;
        trajectory_t apos;
        LerpEntityStateTypeUnion u;
    };

    union $C889CF518587CB2833BFE41358FA5E4A
    {
        int brushmodel;
        int item;
        int xmodel;
        int primaryLight;
    };

    union $0AC61FC53F35A99FE97BBC85FAE448D4
    {
        int scale;
        int eventParm2;
        int helicopterStage;
    };

    /* 1143 */
    union $F73F8AB0498479EECE844017F4CFA302
    {
        int hintString;
        int vehicleXModel;
    };

    struct entityState_s
    {
        int number;
        entityType_t eType;
        LerpEntityState lerp;
        int time2;
        int otherEntityNum;
        int attackerEntityNum;
        int groundEntityNum;
        int loopSound;
        int surfType;
        $C889CF518587CB2833BFE41358FA5E4A index;
        int clientNum;
        int iHeadIcon;
        int iHeadIconTeam;
        int solid;
        unsigned int eventParm;
        int eventSequence;
        int events[4];
        int eventParms[4];
        int weapon;
        int weaponModel;
        int legsAnim;
        int torsoAnim;
        $0AC61FC53F35A99FE97BBC85FAE448D4 un1;
        $F73F8AB0498479EECE844017F4CFA302 un2;
        float fTorsoPitch;
        float fWaistPitch;
        unsigned int partBits[4];
    };

    enum team_t
    {
        TEAM_FREE = 0x0,
        TEAM_AXIS = 0x1,
        TEAM_ALLIES = 0x2,
        TEAM_SPECTATOR = 0x3,
        TEAM_NUM_TEAMS = 0x4,
    };

    struct clientState_s
    {
        int clientIndex;
        team_t team;
        int modelindex;
        int attachModelIndex[6];
        int attachTagIndex[6];
        char name[16];
        float maxSprintTimeMultiplier;
        int rank;
        int prestige;
        int perks;
        int attachedVehEntNum;
        int attachedVehSlotIndex;
    };

    // T4 (CoDWaWmp.exe) layout — reshape 2026-05-23 session 4 (cycle A of
    // FS_Read seek work). Verified offsets all sourced from Caball009's
    // working MP demo-rewind PoC (refs/rewind_caball009/) plus our own
    // per-frame caball-probe data. Fields whose T4 offsets are not yet
    // known are represented as named byte-pads; consumers of those fields
    // read garbage but the struct compiles cleanly.
    //
    // Verified offsets in T4 MP (clientActive_s base at 0x00F44780):
    //   +0x00118  cl.snap.serverTime
    //   +0x03AF4  cl.serverTime              (== mirror at 0x01F00DD0)
    //   +0x03AF8  cl.oldServerTime
    //   +0x03AFC  cl.oldFrameServerTime
    //   +0x03B00  cl.serverTimeDelta         (= serverTime - cls.realtime)
    //   +0x03B10  gameState                  (143300 bytes per Caball009 —
    //                                          larger than IW3's 0x2262C)
    //   +0x26B14  cl.parseEntitiesNum
    //   +0x26B18  cl.parseClientsNum
    //   +0x6B33C  cl.snapshots[32]           (each 14816 = 0x39E0 bytes)
    //
    // Fields with unknown T4 offsets (live at wrong addresses, accessed
    // from GetBoneData / camera code etc.): skelTimeStamp, mapname,
    // cgameOrigin, viewangles, etc. Marked with `// TBD` below. Those
    // features (bone-camera, cgame-predicted-data) won't work correctly
    // until the offsets are reverse-engineered.
    struct clientActive_t
    {
        byte _t4_pad_prefix[0x110];                  // 0x0000..0x010F  unknown
        clSnapshot_t snap;                           // 0x0110  snap.serverTime @ +0x118 ✅
        // After snap (0x0110 + sizeof(clSnapshot_t) = end-of-snap)
        // sizeof(clSnapshot_t) MUST be 0x39E0 in T4 per Caball009; if our
        // bundled struct differs, the static_assert below will fail and
        // we'll need to pad inside clSnapshot_t.
        bool alwaysFalse;                            // 0x3AF0
        char _t4_pad_post_alwaysFalse[3];            // 0x3AF1..0x3AF3
        int serverTime;                              // 0x3AF4 ✅
        int oldServerTime;                           // 0x3AF8 ✅
        int oldFrameServerTime;                      // 0x3AFC ✅
        int serverTimeDelta;                         // 0x3B00 ✅
        int oldSnapServerTime;                       // 0x3B04
        int extrapolatedSnapshot;                    // 0x3B08
        int newSnapshots;                            // 0x3B0C
        // T4 gameState size differs from IW3 (143300 vs 0x2262C). Treated
        // as opaque buffer — Rewinding state machine memcpys it whole; no
        // field-level access needed from us. Original gameState_t struct
        // is no longer applicable to T4 clientActive_t.
        byte gameState[143300];                      // 0x3B10..0x26AD3 ✅
        byte _t4_pad_pre_parseEntities[0x40];        // 0x26AD4..0x26B13
        int parseEntitiesNum;                        // 0x26B14 ✅
        int parseClientsNum;                         // 0x26B18 ✅
        byte _t4_pad_to_snapshots[0x44820];          // 0x26B1C..0x6B33B
        clSnapshot_t snapshots[32];                  // 0x6B33C ✅
        // After snapshots[32] = 0x6B33C + 32*0x39E0 = 0xB7BBC.
        // Remaining T4 fields (skelTimeStamp, skelMemory, cmds[],
        // clientArchive[], outPackets[], entityBaselines[], parseEntities[],
        // parseClients[], mapname, cgameOrigin, viewangles, etc.) have
        // unknown T4 offsets. Provide a tail block + placeholder fields
        // that the existing iw3-derived T4Interface code references — the
        // placeholders will read garbage but at least compile.
        byte _t4_pad_post_snapshots[0x80000];        // 512K of tail slack

        // BELOW HERE: offsets are WRONG for T4 (placeholders to keep code
        // compiling). DO NOT rely on these reads producing real values.
        // To be relocated once each field's T4 offset is verified.
        int skelTimeStamp;                           // TBD — placeholder
        char mapname[64];                            // TBD — placeholder
    };
    static_assert(offsetof(clientActive_t, snap) == 0x110,
                  "T4 clientActive_t.snap must be at +0x110 (so snap.serverTime lands at +0x118)");
    static_assert(offsetof(clSnapshot_t, serverTime) == 0x8,
                  "clSnapshot_t.serverTime must be at +0x8 (CoD-engine layout)");
    static_assert(sizeof(clSnapshot_t) == 0x39E0,
                  "T4 clSnapshot_t must be 0x39E0 (14816) bytes per Caball009 — pad playerState_s if this fails");
    static_assert(offsetof(clientActive_t, serverTime) == 0x3AF4,
                  "T4 clientActive_t.serverTime must be at +0x3AF4");
    static_assert(offsetof(clientActive_t, serverTimeDelta) == 0x3B00,
                  "T4 clientActive_t.serverTimeDelta must be at +0x3B00");
    static_assert(offsetof(clientActive_t, gameState) == 0x3B10,
                  "T4 clientActive_t.gameState must be at +0x3B10");
    static_assert(offsetof(clientActive_t, parseEntitiesNum) == 0x26B14,
                  "T4 clientActive_t.parseEntitiesNum must be at +0x26B14");
    static_assert(offsetof(clientActive_t, parseClientsNum) == 0x26B18,
                  "T4 clientActive_t.parseClientsNum must be at +0x26B18");
    static_assert(offsetof(clientActive_t, snapshots) == 0x6B33C,
                  "T4 clientActive_t.snapshots must be at +0x6B33C");

    struct XModel
    {
        const char* name;
        char numBones;
        char numRootBones;
        char numsurfs;
        char lodRampType;
        unsigned __int16* boneNames;
        // ...
    };
    
    struct DSkelPartBits
    {
        int anim[4];
        int control[4];
        int skel[4];
    };

    struct DSkel
    {
        DSkelPartBits partBits;
        int timeStamp;
        void* mat;  // DObjAnimMat
    };

    struct DObj_s
    {
        void* tree;                       // +0x00  XAnimTree_s*
        unsigned __int16 duplicateParts;  // +0x04
        unsigned __int16 entnum;          // +0x06
        char duplicatePartsSize;          // +0x08
        char numModels;                   // +0x09
        char numBones;                    // +0x0A
        unsigned int ignoreCollision;     // +0x0C
        volatile int locked;              // +0x10
        DSkel skel;                       // +0x14  (size 0x38)
        float radius;                     // +0x4C
        unsigned int hidePartBits[4];     // +0x50  (size 0x10)
        // T4-specific block 2026-05-24 per T4SP-Server-Plugin/structs.hpp:5129.
        // IW3 had models** at +0x60; T4 inserts 4 bytes here pushing models
        // to +0x64. Bonecam reads models as garbage without this pad.
        char localClientIndex;            // +0x60
        unsigned char flags;              // +0x61
        unsigned __int16 ikStateIndex;    // +0x62
        XModel** models;                  // +0x64  XModel**
    };
    static_assert(sizeof(DObj_s) == 0x68,
                  "T4 DObj_s must be 0x68 (104) bytes per T4SP-Server-Plugin");
    static_assert(offsetof(DObj_s, models) == 0x64,
                  "T4 DObj_s.models must be at +0x64");

    struct cgs_t
    {
        int viewX;
        int viewY;
        int viewWidth;
        int viewHeight;
        float viewAspect;
        int serverCommandSequence;
        int processedSnapshotNum;
        int localServer;
        char gametype[32];
        char szHostName[256];
        int maxclients;
        char mapname[64];
        int gameEndTime;
        int voteTime;
        int voteYes;
        int voteNo;
        char voteString[256];
        XModel* gameModels[512];
        void* fxs[100];
        void* smokeGrenadeFx;
        byte holdBreathParams[0x268];
        char teamChatMsgs[8][160];
        int teamChatMsgTimes[8];
        int teamChatPos;
        int teamLastChatPos;
        float compassWidth;
        float compassHeight;
        float compassY;
        // ...
    };

    enum DemoType
    {
        DEMO_TYPE_NONE = 0x0,
        DEMO_TYPE_CLIENT = 0x1,
        DEMO_TYPE_SERVER = 0x2,
    };

    enum CubemapShot
    {
        CUBEMAPSHOT_NONE = 0x0,
        CUBEMAPSHOT_RIGHT = 0x1,
        CUBEMAPSHOT_LEFT = 0x2,
        CUBEMAPSHOT_BACK = 0x3,
        CUBEMAPSHOT_FRONT = 0x4,
        CUBEMAPSHOT_UP = 0x5,
        CUBEMAPSHOT_DOWN = 0x6,
        CUBEMAPSHOT_COUNT = 0x7,
    };

    struct snapshot_s
    {
        int snapFlags;
        int ping;
        int serverTime;
        playerState_s ps;
        int numEntities;
        int numClients;
        entityState_s entities[512];
        clientState_s clients[64];
        int serverCommandSequence;
    };

    struct playerEntity_t
    {
        float fLastWeaponPosFrac;
        int bPositionToADS;
        float vPositionLastOrg[3];
        float fLastIdleFactor;
        float vLastMoveOrg[3];
        float vLastMoveAng[3];
    };

    struct cpose_t
    {
        unsigned __int16 lightingHandle;
        char eType;
        char eTypeUnion;
        char localClientNum;
        int cullIn;
        char isRagdoll;
        int ragdollHandle;
        int killcamRagdollHandle;
        int physObjId;
        float origin[3];
        float angles[3];
        byte pad[0x30];  // some fields here
    };

    struct centity_s
    {
        cpose_t pose;
        LerpEntityState currentState;
        entityState_s nextState;
        bool nextValid;
        bool bMuzzleFlash;
        bool bTrailMade;
        int previousEventSequence;
        int miscTime;
        float lightingOrigin[3];
        void* tree; // XAnimTree_s
        // T4 port 2026-05-24: T4SP-Server-Plugin documents sizeof(centity_s)
        // = 0x304 (772 bytes). Our IW3-derived fields above sum to ~476 bytes,
        // so we'd index by wrong stride when reading cg_entities[i]. Pad to
        // the verified T4 size so array indexing matches reality. Inner-field
        // offsets are still IW3-style (some may read garbage) — but the
        // entity-POINTER passed into T4's CG_DObjGetWorldBoneMatrix lands at
        // the right slot, which is what bonecam needs.
        byte _t4_pad_end[0x304 - 476];
    };
    static_assert(sizeof(centity_s) == 0x304,
                  "T4 centity_s must be 0x304 (772) bytes per T4SP-Server-Plugin");

    struct score_t
    {
        byte pad[0x28];
    };

    struct viewDamage_t
    {
        int time;
        int duration;
        float yaw;
    };

    struct shellshock_t
    {
        byte pad[0x20];
    };

    struct $F6DFD6D87F75480A1EF1906639406DF5
    {
        int time;
        int duration;
    };

    struct animation_t
    {
        char name[64];
        int initialLerp;
        float moveSpeed;
        int duration;
        int nameHash;
        int flags;
        int64_t movetype;
        int noteType;
    };

    struct animScriptData_t
    {
        animation_t animations[512];
        byte pad[0x9A9D0 - sizeof(animations)];
    };

    struct $0867E0FC4F8157A276DAB76B1612E229
    {
        byte pad[0x10];
    };

    struct lerpFrame_t
    {
        float yawAngle;
        int yawing;
        float pitchAngle;
        int pitching;
        int animationNumber;
        void* animation;
        int animationTime;
        float oldFramePos[3];
        float animSpeedScale;
        int oldFrameSnapshotTime;
    };

    struct clientControllers_t
    {
        float angles[6][3];
        float tag_origin_angles[3];
        float tag_origin_offset[3];
    };

    struct __declspec(align(4)) clientInfo_t
    {
        int infoValid;
        int nextValid;
        int clientNum;
        char name[16];
        team_t team;
        team_t oldteam;
        int rank;
        int prestige;
        int perks;
        int score;
        int location;
        int health;
        char model[64];
        char attachModelNames[6][64];
        char attachTagNames[6][64];
        lerpFrame_t legs;
        lerpFrame_t torso;
        float lerpMoveDir;
        float lerpLean;
        float playerAngles[3];
        int leftHandGun;
        int dobjDirty;
        clientControllers_t control;
        unsigned int clientConditions[10][2];
        void* pXAnimTree;
        int iDObjWeapon;
        char weaponModel;
        int stanceTransitionTime;
        int turnAnimEndTime;
        char turnAnimType;
        int attachedVehEntNum;
        int attachedVehSlotIndex;
        bool hideWeapon;
        bool usingKnife;
    };

    struct bgs_t
    {
        animScriptData_t animScriptData;
        $0867E0FC4F8157A276DAB76B1612E229 generic_human;
        int time;
        int latestSnapshotTime;
        int frametime;
        int anim_user;
        XModel*(__cdecl* GetXModel)(const char*);
        void(__cdecl* CreateDObj)(void*, unsigned __int16, void*, int, int, void*);
        unsigned __int16(__cdecl* AttachWeapon)(void*, unsigned __int16, void*);
        DObj_s*(__cdecl* GetDObj)(int, int);
        void(__cdecl* SafeDObjFree)(int, int);
        void*(__cdecl* AllocXAnim)(int);
        clientInfo_t clientinfo[64];
    };

    struct cg_s
    {
        int clientNum;
        int localClientNum;
        DemoType demoType;
        CubemapShot cubemapShot;
        int cubemapSize;
        int renderScreen;
        int latestSnapshotNum;
        int latestSnapshotTime;
        snapshot_s* snap;
        snapshot_s* nextSnap;
        snapshot_s activeSnapshots[2];
        float frameInterpolation;
        int frametime;
        int time;
        int oldTime;
        int physicsTime;
        int mapRestart;
        int renderingThirdPerson;
        playerState_s predictedPlayerState;
        centity_s predictedPlayerEntity;
        playerEntity_t playerEntity;
        int predictedErrorTime;
        float predictedError[3];
        float landChange;
        int landTime;
        float heightToCeiling;
        refdef_s refdef;
        float refdefViewAngles[3];
        float lastVieworg[3];
        float swayViewAngles[3];
        float swayAngles[3];
        float swayOffset[3];
        int iEntityLastType[1024];
        XModel* pEntityLastXModel[1024];
        float zoomSensitivity;
        bool isLoading;
        char objectiveText[1024];
        char scriptMainMenu[256];
        int scoresRequestTime;
        int numScores;
        int teamScores[4];
        int teamPings[4];
        int teamPlayers[4];
        score_t scores[64];
        int scoreLimit;
        int showScores;
        int scoreFadeTime;
        int scoresTop;
        int scoresOffBottom;
        int scoresBottom;
        int drawHud;
        int crosshairClientNum;
        int crosshairClientLastTime;
        int crosshairClientStartTime;
        int identifyClientNum;
        int cursorHintIcon;
        int cursorHintTime;
        int cursorHintFade;
        int cursorHintString;
        int lastClipFlashTime;
        int invalidCmdHintType;
        int invalidCmdHintTime;
        int lastHealthPulseTime;
        int lastHealthLerpDelay;
        int lastHealthClient;
        float lastHealth;
        float healthOverlayFromAlpha;
        float healthOverlayToAlpha;
        int healthOverlayPulseTime;
        int healthOverlayPulseDuration;
        int healthOverlayPulsePhase;
        bool healthOverlayHurt;
        int healthOverlayLastHitTime;
        float healthOverlayOldHealth;
        int healthOverlayPulseIndex;
        int proneBlockedEndTime;
        int lastStance;
        int lastStanceChangeTime;
        int lastStanceFlashTime;
        int voiceTime;
        unsigned int weaponSelect;
        int weaponSelectTime;
        unsigned int weaponLatestPrimaryIdx;
        int prevViewmodelWeapon;
        int equippedOffHand;
        viewDamage_t viewDamage[8];
        int damageTime;
        float damageX;
        float damageY;
        float damageValue;
        float viewFade;
        int weapIdleTime;
        int nomarks;
        int v_dmg_time;
        float v_dmg_pitch;
        float v_dmg_roll;
        float fBobCycle;
        float xyspeed;
        float kickAVel[3];
        float kickAngles[3];
        float offsetAngles[3];
        float gunPitch;
        float gunYaw;
        float gunXOfs;
        float gunYOfs;
        float gunZOfs;
        float vGunOffset[3];
        float vGunSpeed[3];
        float viewModelAxis[4][3];
        float rumbleScale;
        float compassNorthYaw;
        float compassNorth[2];
        void* compassMapMaterial; // Material*
        float compassMapUpperLeft[2];
        float compassMapWorldSize[2];
        int compassFadeTime;
        int healthFadeTime;
        int ammoFadeTime;
        int stanceFadeTime;
        int sprintFadeTime;
        int offhandFadeTime;
        int offhandFlashTime;
        shellshock_t shellshock;
        $F6DFD6D87F75480A1EF1906639406DF5 testShock;
        int holdBreathTime;
        int holdBreathInTime;
        int holdBreathDelay;
        float holdBreathFrac;
        float radarProgress;
        float selectedLocation[2];
        SprintState sprintStates;
        int _unk01;
        int _unk02;
        bgs_t bgs;
        // ...
    };

    struct directory_t
    {
        char path[256];
        char gamedir[256];
    };

    struct iwd_t
    {
        char iwdFilename[256];
        char iwdBasename[256];
        char iwdGamename[256];
        char* handle;
        int checksum;
        int pure_checksum;
        volatile int hasOpenFile;
        int numfiles;
        char referenced;
        unsigned int hashSize;
        void** hashTable;
        void* buildBuffer;
    };

    struct searchpath_s
    {
        searchpath_s* next;
        iwd_t* iwd;
        directory_t* dir;
        int bLocalized;
        int ignore;
        int ignorePureCheck;
        int language;
    };

    union DvarValue
    {
        bool enabled;
        int integer;
        unsigned int unsignedInt;
        float value;
        float vector[4];
        const char* string;
        char color[4];
    };

    /* 754 */
    struct $BFBB53559BEAC4289F32B924847E59CB
    {
        int stringCount;
        const char** strings;
    };

    /* 755 */
    struct $9CA192F9DB66A3CB7E01DE78A0DEA53D
    {
        int min;
        int max;
    };

    /* 756 */
    struct $251C2428A496074035CACA7AAF3D55BD
    {
        float min;
        float max;
    };

    /* 757 */
    union DvarLimits
    {
        $BFBB53559BEAC4289F32B924847E59CB enumeration;
        $9CA192F9DB66A3CB7E01DE78A0DEA53D integer;
        $251C2428A496074035CACA7AAF3D55BD value;
        $251C2428A496074035CACA7AAF3D55BD vector;
    };

    /* 758 */
    // T4 (CoDWaWmp.exe) layout — verified against t4-rtx/src/game/structs.hpp
    // (STATIC_ASSERT_SIZE 0x5C). Differs from IW3 in two ways:
    //   1) 4-byte alignment pad inserted before `current` (current is at
    //      offset 0x10 in T4 vs 0x0C in IW3).
    //   2) Extra `saved` DvarValue between `reset` and `domain`.
    //   3) No `domainFunc` field after `domain`.
    // All downstream reads of dvar->current.{value,integer,enabled,...}
    // were silently returning garbage from the pad bytes prior to this fix.
    struct dvar_s
    {
        const char* name;        // 0x00
        const char* description; // 0x04
        unsigned __int16 flags;  // 0x08
        char type;               // 0x0A
        bool modified;           // 0x0B
        char pad[4];             // 0x0C  — T4-specific
        DvarValue current;       // 0x10
        DvarValue latched;       // 0x20
        DvarValue reset;         // 0x30
        DvarValue saved;         // 0x40  — T4-specific
        DvarLimits domain;       // 0x50
        dvar_s* hashNext;        // 0x58
    };
    static_assert(sizeof(dvar_s) == 0x5C, "T4 dvar_s must be 0x5C bytes");

    struct __declspec(align(4)) WinMouseVars_t
    {
        int oldButtonState;
        tagPOINT oldPos;
        bool mouseActive;
        bool mouseInitialized;
    };

    struct clientUIActive_t
    {
        bool active;
        bool isRunning;
        bool cgameInitialized;
        bool cgameInitCalled;
        int keyCatchers;
        bool displayHUDWithKeycatchUI;
        // ...
    };

    union qfile_gus
    {
        _iobuf* o;
        char* z;
    };

    struct qfile_us
    {
        qfile_gus file;
        int iwdIsClone;
    };

        enum MaterialTechniqueType
    {
        TECHNIQUE_DEPTH_PREPASS = 0x0,
        TECHNIQUE_BUILD_FLOAT_Z = 0x1,
        TECHNIQUE_BUILD_SHADOWMAP_DEPTH = 0x2,
        TECHNIQUE_BUILD_SHADOWMAP_COLOR = 0x3,
        TECHNIQUE_UNLIT = 0x4,
        TECHNIQUE_EMISSIVE = 0x5,
        TECHNIQUE_EMISSIVE_SHADOW = 0x6,
        TECHNIQUE_LIT_BEGIN = 0x7,
        TECHNIQUE_LIT = 0x7,
        TECHNIQUE_LIT_SUN = 0x8,
        TECHNIQUE_LIT_SUN_SHADOW = 0x9,
        TECHNIQUE_LIT_SPOT = 0xA,
        TECHNIQUE_LIT_SPOT_SHADOW = 0xB,
        TECHNIQUE_LIT_OMNI = 0xC,
        TECHNIQUE_LIT_OMNI_SHADOW = 0xD,
        TECHNIQUE_LIT_INSTANCED = 0xE,
        TECHNIQUE_LIT_INSTANCED_SUN = 0xF,
        TECHNIQUE_LIT_INSTANCED_SUN_SHADOW = 0x10,
        TECHNIQUE_LIT_INSTANCED_SPOT = 0x11,
        TECHNIQUE_LIT_INSTANCED_SPOT_SHADOW = 0x12,
        TECHNIQUE_LIT_INSTANCED_OMNI = 0x13,
        TECHNIQUE_LIT_INSTANCED_OMNI_SHADOW = 0x14,
        TECHNIQUE_LIT_END = 0x15,
        TECHNIQUE_LIGHT_SPOT = 0x15,
        TECHNIQUE_LIGHT_OMNI = 0x16,
        TECHNIQUE_LIGHT_SPOT_SHADOW = 0x17,
        TECHNIQUE_FAKELIGHT_NORMAL = 0x18,
        TECHNIQUE_FAKELIGHT_VIEW = 0x19,
        TECHNIQUE_SUNLIGHT_PREVIEW = 0x1A,
        TECHNIQUE_CASE_TEXTURE = 0x1B,
        TECHNIQUE_WIREFRAME_SOLID = 0x1C,
        TECHNIQUE_WIREFRAME_SHADED = 0x1D,
        TECHNIQUE_SHADOWCOOKIE_CASTER = 0x1E,
        TECHNIQUE_SHADOWCOOKIE_RECEIVER = 0x1F,
        TECHNIQUE_DEBUG_BUMPMAP = 0x20,
        TECHNIQUE_DEBUG_BUMPMAP_INSTANCED = 0x21,
        TECHNIQUE_COUNT = 0x22,
        TECHNIQUE_TOTAL_COUNT = 0x23,
        TECHNIQUE_NONE = 0x24,
    };

    struct __declspec(align(16)) GfxCmdBufSourceState
    {
        // ...
    };

    struct $C6651C03833075F2AE4768F11066A2E3
    {
        unsigned int stride;
        IDirect3DVertexBuffer9* vb;
        unsigned int offset;
    };

    enum MaterialVertexDeclType
    {
        VERTDECL_GENERIC = 0x0,
        VERTDECL_PACKED = 0x1,
        VERTDECL_WORLD = 0x2,
        VERTDECL_WORLD_T1N0 = 0x3,
        VERTDECL_WORLD_T1N1 = 0x4,
        VERTDECL_WORLD_T2N0 = 0x5,
        VERTDECL_WORLD_T2N1 = 0x6,
        VERTDECL_WORLD_T2N2 = 0x7,
        VERTDECL_WORLD_T3N0 = 0x8,
        VERTDECL_WORLD_T3N1 = 0x9,
        VERTDECL_WORLD_T3N2 = 0xA,
        VERTDECL_WORLD_T4N0 = 0xB,
        VERTDECL_WORLD_T4N1 = 0xC,
        VERTDECL_WORLD_T4N2 = 0xD,
        VERTDECL_POS_TEX = 0xE,
        VERTDECL_STATICMODELCACHE = 0xF,
        VERTDECL_COUNT = 0x10,
    };

    struct GfxCmdBufPrimState
    {
        IDirect3DDevice9* device;
        IDirect3DIndexBuffer9* indexBuffer;
        MaterialVertexDeclType vertDeclType;
        $C6651C03833075F2AE4768F11066A2E3 streams[2];
        IDirect3DVertexDeclaration9* vertexDecl;
    };

    struct GfxDrawSurfFields
    {
        unsigned __int64 objectId : 16;
        unsigned __int64 reflectionProbeIndex : 8;
        unsigned __int64 customIndex : 5;
        unsigned __int64 materialSortedIndex : 11;
        unsigned __int64 prepass : 2;
        unsigned __int64 primaryLightIndex : 8;
        unsigned __int64 surfType : 4;
        unsigned __int64 primarySortKey : 6;
        unsigned __int64 unused : 4;
    };

    union GfxDrawSurf
    {
        GfxDrawSurfFields fields;
        unsigned __int64 packed;
    };

    struct MaterialInfo
    {
        const char* name;
        char gameFlags;
        char sortKey;
        char textureAtlasRowCount;
        char textureAtlasColumnCount;
        GfxDrawSurf drawSurf;
        unsigned int surfaceTypeBits;
        unsigned __int16 hashIndex;
    };

    struct MaterialPass
    {
        void** vertexDecl;   // MaterialVertexDeclaration
        void* vertexShader;  // MaterialVertexShader
        void* pixelShader;   // MaterialPixelShader
        char perPrimArgCount;
        char perObjArgCount;
        char stableArgCount;
        char customSamplerFlags;
        void* args;  // MaterialShaderArgument
    };

    struct MaterialTechnique
    {
        const char* name;
        unsigned __int16 flags;
        unsigned __int16 passCount;
        MaterialPass passArray[1];
    };

    struct MaterialTechniqueSet
    {
        const char* name;
        char worldVertFormat;
        bool hasBeenUploaded;
        char unused[1];
        MaterialTechniqueSet* remappedTechniqueSet;
        MaterialTechnique* techniques[34];
    };

    struct Material
    {
        MaterialInfo info;
        char stateBitsEntry[34];
        char textureCount;
        char constantCount;
        char stateBitsCount;
        char stateFlags;
        char cameraRegion;
        MaterialTechniqueSet* techniqueSet;
        void* textureTable;    // MaterialTextureDef
        void* constantTable;   // MaterialConstantDef
        void* stateBitsTable;  // GfxStateBits
    };

    enum GfxDepthRangeType
    {
        GFX_DEPTH_RANGE_SCENE = 0x0,
        GFX_DEPTH_RANGE_VIEWMODEL = 0x2,
        GFX_DEPTH_RANGE_FULL = 0xFFFFFFFF,
    };

    struct GfxViewport
    {
        int x;
        int y;
        int width;
        int height;
    };

    enum GfxRenderTargetId
    {
        R_RENDERTARGET_SAVED_SCREEN = 0x0,
        R_RENDERTARGET_FRAME_BUFFER = 0x1,
        R_RENDERTARGET_SCENE = 0x2,
        R_RENDERTARGET_RESOLVED_POST_SUN = 0x3,
        R_RENDERTARGET_RESOLVED_SCENE = 0x4,
        R_RENDERTARGET_FLOAT_Z = 0x5,
        R_RENDERTARGET_DYNAMICSHADOWS = 0x6,
        R_RENDERTARGET_PINGPONG_0 = 0x7,
        R_RENDERTARGET_PINGPONG_1 = 0x8,
        R_RENDERTARGET_SHADOWCOOKIE = 0x9,
        R_RENDERTARGET_SHADOWCOOKIE_BLUR = 0xA,
        R_RENDERTARGET_POST_EFFECT_0 = 0xB,
        R_RENDERTARGET_POST_EFFECT_1 = 0xC,
        R_RENDERTARGET_SHADOWMAP_SUN = 0xD,
        R_RENDERTARGET_SHADOWMAP_SPOT = 0xE,
        R_RENDERTARGET_COUNT = 0xF,
        R_RENDERTARGET_NONE = 0x10,
    };

    struct GfxCmdBufState
    {
        char refSamplerState[16];
        unsigned int samplerState[16];
        void* samplerTexture[16];  // GfxTexture
        GfxCmdBufPrimState prim;
        Material* material;
        MaterialTechniqueType techType;
        void* technique;  // MaterialTechnique
        void* pass;       // MaterialPass
        unsigned int passIndex;
        GfxDepthRangeType depthRangeType;
        float depthRangeNear;
        float depthRangeFar;
        unsigned __int64 vertexShaderConstState[32];
        unsigned __int64 pixelShaderConstState[256];
        char alphaRef;
        unsigned int refStateBits[2];
        unsigned int activeStateBits[2];
        void* pixelShader;   // MaterialPixelShader
        void* vertexShader;  // MaterialVertexShader
        GfxViewport viewport;
        GfxRenderTargetId renderTargetId;
        Material* origMaterial;
        MaterialTechniqueType origTechType;
    };

    struct __declspec(align(128)) r_global_permanent_t
    {
        Material* sortedMaterials[2048];
        int needSortMaterials;
        int materialCount;
        void* whiteImage;               // GfxImage
        void* blackImage;               // GfxImage
        void* blackImage3D;             // GfxImage
        void* blackImageCube;           // GfxImage
        void* grayImage;                // GfxImage
        void* identityNormalMapImage;   // GfxImage
        void* specularityImage;         // GfxImage
        void* outdoorImage;             // GfxImage
        void* pixelCostColorCodeImage;  // GfxImage
        void* dlightDef;                // GfxLightDef
        Material* defaultMaterial;
        Material* whiteMaterial;
        Material* additiveMaterial;
        Material* pointMaterial;
        Material* lineMaterial;
        Material* lineMaterialNoDepth;
        Material* clearAlphaStencilMaterial;
        Material* shadowClearMaterial;
        Material* shadowCookieOverlayMaterial;
        Material* shadowCookieBlurMaterial;
        Material* shadowCasterMaterial;
        Material* shadowOverlayMaterial;
        Material* depthPrepassMaterial;
        Material* glareBlindMaterial;
        Material* pixelCostAddDepthAlwaysMaterial;
        Material* pixelCostAddDepthDisableMaterial;
        Material* pixelCostAddDepthEqualMaterial;
        Material* pixelCostAddDepthLessMaterial;
        Material* pixelCostAddDepthWriteMaterial;
        Material* pixelCostAddNoDepthWriteMaterial;
        Material* pixelCostColorCodeMaterial;
        Material* stencilShadowMaterial;
        Material* stencilDisplayMaterial;
        Material* floatZDisplayMaterial;
        Material* colorChannelMixerMaterial;
        Material* frameColorDebugMaterial;
        Material* frameAlphaDebugMaterial;
        void* rawImage;  // GfxImage
        void* world;     // GfxWorld
        Material* feedbackReplaceMaterial;
        Material* feedbackBlendMaterial;
        Material* feedbackFilmBlendMaterial;
        Material* cinematicMaterial;
        Material* dofDownsampleMaterial;
        Material* dofNearCocMaterial;
        Material* smallBlurMaterial;
        Material* postFxDofMaterial;
        Material* postFxDofColorMaterial;
        Material* postFxColorMaterial;
        Material* postFxMaterial;
        Material* symmetricFilterMaterial[8];
        Material* shellShockBlurredMaterial;
        Material* shellShockFlashedMaterial;
        Material* glowConsistentSetupMaterial;
        Material* glowApplyBloomMaterial;
        int savedScreenTimes[4];
    };

    struct fileHandleData_t
    {
        qfile_us handleFiles;
        int handleSync;
        int fileSize;
        int zipFilePos;
        iwd_t* zipFile;
        int streamed;
        char name[256];
    };

    clientConnection_t* GetClientConnection();
    clientStatic_t* GetClientStatic();
    clientActive_t* GetClientActive();
    cgs_t* GetClientGlobalsStatic();
    cg_s* GetClientGlobals();
    WinMouseVars_t* GetMouseVars();
    clientUIActive_t* GetClientUIActives();
    centity_s* GetEntities();
    uint16_t* GetClientObjectMap();
    DObj_s* GetObjBuf();
    refdef_s* GetRefdef();
}  // namespace IWXMVM::T4::Structures
