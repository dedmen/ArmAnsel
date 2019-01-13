#include <intercept.hpp>
#include <AnselSDK.h>

using namespace intercept;
HWND ArmaHwnd;
DWORD mainThreadID;

BOOL CALLBACK EnumWindowsProc(
    _In_ HWND   hwnd,
    _In_ LPARAM lParam
) {


    DWORD pid{};

    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    char buf[256]{ 0 };
    GetWindowModuleFileNameA(hwnd, buf, 256);
    char buf2[256]{ 0 };
    GetWindowTextA(hwnd, buf2, 256);
    RECT r;
    GetWindowRect(hwnd, &r);
    if (r.bottom == 0 && r.left == 0) return true;

    if (tid == mainThreadID) {
        ArmaHwnd = hwnd;
        return false;
    }
    return true;
}


class cameraController {
public:
    cameraController(vector3 position) {
        camObj = sqf::cam_create("camera"sv, position);

        setView();
    }
    ~cameraController() {
        leaveView();
        sqf::cam_destroy(camObj);
    }
    //Move view into camera
    void setView() {
        if (!inView)
            sqf::camera_effect(camObj, "internal"sv, "back"sv);
        inView = true;
    }
    //Return view to player camera
    void leaveView() {
        if (inView)
            sqf::camera_effect(camObj, "terminate"sv, "back"sv);
        inView = false;
    }
	vector3 getUp() {
		return sqf::vector_up_visual(camObj);
	}
	vector3 getDir() {
		return sqf::vector_dir_visual(camObj);
	}
    nv::Quat getQuat() {
        auto dir = sqf::vector_dir_visual(camObj);
        auto up = sqf::vector_up_visual(camObj);
        auto right = dir.cross(up);
        nv::Vec3 dirN = { dir.x,dir.z,dir.y };
        nv::Vec3 upN = { up.x,up.z,up.y };
        nv::Vec3 rightN = { right.x,right.z,right.y };
        nv::Quat qN;
        ansel::rotationMatrixVectorsToQuaternion(
            rightN, upN, dirN
            , qN);
        return qN;
    }
    void setQuat(const nv::Quat& quat) {
        nv::Vec3 dirN, upN, rightN;
        ansel::quaternionToRotationMatrixVectors(quat, rightN, upN, dirN);

        vector3 dir = { dirN.x,dirN.z,dirN.y };
        vector3 up = { upN.x,upN.z,upN.y };

		curDir[0] = dir.normalize();
		curDir[1] = up.normalize();

        sqf::set_vector_dir_and_up(camObj, dir, up);
    }
    vector3 getPos() {
        return sqf::get_pos_asl_visual(camObj);
    }
    void setPos(const vector3& pos) {
        return sqf::set_pos_asl(camObj, pos);
    }
	void setFOV(float angle) {
		float zoom = angle / 64.3819f;
		curFOV = angle;
		sqf::cam_set_fov(camObj, zoom);
	}
	float degToRad(float deg) {
		return (3.14159265358979323846f / 180.f) * deg;
	}

	void setOffsetProjection(float ox, float oy) {

		if (ox == 0.f && oy == 0.f) {
			projOffsetActive = false;
			//No need to set dir. Because we set it together with the quat anyway
			//sqf::set_vector_dir_and_up(camObj, curDir[0], curDir[1]);
			return;
		}

		float vertFOV = curFOV * (1050.f / 1680.f);

		float angleOffsetX = ox * curFOV;
		float angleOffsetY = oy * vertFOV;

		float angleOffsetXR = degToRad(angleOffsetX);
		float angleOffsetYR = degToRad(angleOffsetY);

		auto dir = curDir[0];
		auto up = curDir[1];
		auto right = up.cross(dir);
		//dir,right,up
		

		//rotate up/down
		//around X

		dir = { 
			dir.x,
			dir.y*std::cosf(angleOffsetYR) - dir.z*std::sinf(angleOffsetYR),

			dir.y*std::sinf(angleOffsetYR) + dir.z*std::cosf(angleOffsetYR)		
		};
		up = dir.cross(right);



		//rotate left/right
		//around Z
		dir = {dir.x * std::cosf(angleOffsetXR) -dir.y*std::sinf(angleOffsetXR),
			dir.x * std::sinf(angleOffsetXR) + dir.y*std::cosf(angleOffsetXR),			
			dir.z};


		sqf::set_vector_dir_and_up(camObj, dir, up);
		projOffsetActive = true;
	}
	float curFOV = 64.3819f;
    bool inView = false;
	vector3 curDir[2]; //dir/up
    object camObj;
	bool projOffsetActive = false;
};

std::unique_ptr<cameraController> camController;


bool anselActive = false;
bool hidingPlayerBody = false;

ansel::StartSessionStatus startSession(ansel::SessionConfiguration& settings, void* userPointer) {
    //#TODO only in mission when in own body and velocity is 0.

    anselActive = true;

	// User can move the camera during session
	settings.isTranslationAllowed = true;
	// Camera can be rotated during session
	settings.isRotationAllowed = true;
	// FoV can be modified during session
	settings.isFovChangeAllowed = true;
	// Game is paused during session
	settings.isPauseAllowed = true;
	// Game allows highres capture during session
	settings.isHighresAllowed = true;
	// Game allows 360 capture during session
	settings.is360MonoAllowed = true;
	// Game allows 360 stereo capture during session
	settings.is360StereoAllowed = true;
	// Game allows capturing pre-tonemapping raw HDR buffer
	settings.isRawAllowed = true;

   
    camController = std::make_unique<cameraController>(sqf::atl_to_asl(sqf::eye_pos(sqf::player())));

    return ansel::kAllowed;
}
void stopSession(void* userPointer) {
    anselActive = false;
    camController = nullptr;

    if (hidingPlayerBody)
        sqf::hide_object(sqf::player(), false);
    hidingPlayerBody = false;

}
void startCapture(const ansel::CaptureConfiguration& cfg, void* userPointer) {
	sqf::diag_log("start");
}
void stopCapture(void* userPointer) {
	sqf::diag_log("stop");
}






int intercept::api_version() {
    return 1;
}

void  intercept::on_frame() {
    if (anselActive && camController) {


        ansel::Camera cam{};
        auto camPos = camController->getPos();
        cam.position = { camPos.x,camPos.y,camPos.z };
        cam.fov = 64.3819f;
        cam.projectionOffsetX = cam.projectionOffsetY = 0.f;


        cam.rotation = camController->getQuat();

		sqf::diag_log({ "prev",camPos,camController->getUp(), camController->getDir() });

        ansel::updateCamera(cam);
        camController->setQuat(cam.rotation);

        vector3 newPos = { cam.position.x, cam.position.y, cam.position.z };
        camController->setPos(newPos);
		camController->setFOV(cam.fov);
		camController->setOffsetProjection(cam.projectionOffsetX, -cam.projectionOffsetY);
		
		sqf::diag_log({ "post",camPos,camController->getUp(), camController->getDir(), cam.projectionOffsetX, cam.projectionOffsetY, cam.fov });
    }
}


uintptr_t placeHookTotalOffs(uintptr_t totalOffset, uintptr_t jmpTo) {
	DWORD dwVirtualProtectBackup;


	/*
	32bit
	jmp 0x123122
	0:  e9 1e 31 12 00          jmp    123123 <_main+0x123123>
	64bit
	FF 25 64bit relative
	*/
#ifdef _M_X64
	//auto distance = std::max(totalOffset, jmpTo) - std::min(totalOffset, jmpTo);
	// if distance < 2GB (2147483648) we could use the 32bit relative jmp
	VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, 0x40u, &dwVirtualProtectBackup);
	auto jmpInstr = reinterpret_cast<unsigned char*>(totalOffset);
	auto addrOffs = reinterpret_cast<uint32_t*>(totalOffset + 1);
	*jmpInstr = 0x68; //push DWORD
	*addrOffs = static_cast<uint32_t>(jmpTo) /*- totalOffset - 6*/;//offset
	*reinterpret_cast<uint32_t*>(totalOffset + 5) = 0x042444C7; //MOV [RSP+4],
	*reinterpret_cast<uint32_t*>(totalOffset + 9) = static_cast<uint64_t>(jmpTo) >> 32;//DWORD
	*reinterpret_cast<unsigned char*>(totalOffset + 13) = 0xc3;//ret
	VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
	return totalOffset + 14;
#else
	VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, 0x40u, &dwVirtualProtectBackup);
	auto jmpInstr = reinterpret_cast<unsigned char *>(totalOffset);
	auto addrOffs = reinterpret_cast<unsigned int *>(totalOffset + 1);
	*jmpInstr = 0xE9;
	*addrOffs = jmpTo - totalOffset - 5;
	VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
	return totalOffset + 5;
#endif
}

void intercept::pre_start() {

	 auto myFunc = []() -> HMODULE
	 {
		 auto handle = GetModuleHandleW(L"AnselSDK64.dll");
		 return handle;
	 };

	 auto nvcam = GetModuleHandleW(L"NvCamera64.dll");;
	 auto offs = nvcam + 0x23420;

	// placeHookTotalOffs((uintptr_t) offs, (uintptr_t) &myFunc);

    mainThreadID = GetCurrentThreadId();
    EnumWindows(EnumWindowsProc, 0); //Intercept cba stuff to get Arma hwnd

    if (ansel::isAnselAvailable()) {
        //__debugbreak();



        ansel::Configuration config{};
        // Configure values that we want different from defaults:
        config.translationalSpeedInWorldUnitsPerSecond = 5.0f;
        config.right = { 1,0,0 };
        config.up = { 0,0,1 };
        config.forward = { 0,1,0 };

        config.fovType = ansel::kVerticalFov;
        config.isCameraOffcenteredProjectionSupported = true;
        config.isCameraRotationSupported = true;
        config.isCameraTranslationSupported = true;
        config.isCameraFovSupported = true;

        config.gameWindowHandle = ArmaHwnd;
        config.titleNameUtf8 = u8"Best Game Ever";

        config.startSessionCallback = startSession;
        config.stopSessionCallback = stopSession;
        config.startCaptureCallback = startCapture;
        config.stopCaptureCallback = stopCapture;

        config.captureLatency = 2;


        auto status = ansel::setConfiguration(config);
        if (status != ansel::kSetConfigurationSuccess)
            __debugbreak();


        ansel::UserControlDesc characterCheckbox{};
        characterCheckbox.labelUtf8 = "Show character";
        characterCheckbox.info.userControlId = 0;
        characterCheckbox.info.userControlType = ansel::kUserControlBoolean;
        const bool defaultValue = true;
        characterCheckbox.info.value = &defaultValue;
        characterCheckbox.callback = [](const ansel::UserControlInfo& info) {
            bool val = *reinterpret_cast<const bool*>(info.value);
            if (!val) {
                sqf::hide_object(sqf::player(), true);
                hidingPlayerBody = true;
            } else {
                sqf::hide_object(sqf::player(), false);
                hidingPlayerBody = false;
            }
        };
        auto uistat = ansel::addUserControl(characterCheckbox);

		if (!ansel::isAnselAvailable())	__debugbreak();


    }

	static auto hnd1 = intercept::client::host::register_sqf_command("startAnsel", "startansel", [](uintptr_t) -> game_value {
		ansel::startSession(); return {};
	}, game_data_type::NOTHING);
	static auto hnd2 = intercept::client::host::register_sqf_command("stopAnsel", "stopansel", [](uintptr_t) -> game_value {
		ansel::stopSession(); return {};
	}, game_data_type::NOTHING);
}

void intercept::pre_init() {
	__SQF(
		["ArmAnsel", "StartAnsel", "Start Ansel", { startansel; }, { true }, [0, [false, false, false]], false] call cba_fnc_addKeybind;
		["ArmAnsel", "StopAnsel", "Stop Ansel", { stopansel; }, { true }, [0, [false, false, false]], false] call cba_fnc_addKeybind;
	);



}

void intercept::post_init() {
    
}

void intercept::mission_ended() {

}
