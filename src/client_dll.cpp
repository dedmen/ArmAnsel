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
    nv::Quat getQuat() {
        auto dir = sqf::vector_dir_visual(camObj);
        auto up = sqf::vector_up_visual(camObj);
        auto right = dir.cross(up);
        nv::Vec3 dirN = { dir.x,dir.y,dir.z };
        nv::Vec3 upN = { up.x,up.y,up.z };
        nv::Vec3 rightN = { right.x,right.y,right.z };
        nv::Quat qN;
        ansel::rotationMatrixVectorsToQuaternion(
            rightN, upN, dirN
            , qN);
        return qN;
    }
    void setQuat(const nv::Quat& quat) {
        nv::Vec3 dirN, upN, rightN;
        ansel::quaternionToRotationMatrixVectors(quat, rightN, upN, dirN);

        vector3 dir = { dirN.x,dirN.y,dirN.z };
        vector3 up = { upN.x,upN.y,upN.z };

        sqf::set_vector_dir_and_up(camObj, dir, up);
    }
    vector3 getPos() {
        return sqf::get_pos_asl_visual(camObj);
    }
    void setPos(const vector3& pos) {
        return sqf::set_pos_asl(camObj, pos);
    }
    bool inView = false;
    object camObj;

};

std::unique_ptr<cameraController> camController;


bool anselActive = false;
bool hidingPlayerBody = false;

ansel::StartSessionStatus startSession(ansel::SessionConfiguration& settings, void* userPointer) {
    //#TODO only in mission when in own body and velocity is 0.

    anselActive = true;
    settings.isFovChangeAllowed = false;
   
    camController = std::make_unique<cameraController>(sqf::eye_pos(sqf::player()));

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

}
void stopCapture(void* userPointer) {
    
}






int intercept::api_version() {
    return 1;
}

void  intercept::on_frame() {
    if (anselActive && camController) {


        ansel::Camera cam{};
        auto camPos = camController->getPos();
        cam.position = { camPos.x,camPos.y,camPos.z };
        cam.fov = 70;
        cam.projectionOffsetX = cam.projectionOffsetY = 0.f;


        cam.rotation = camController->getQuat();

        ansel::updateCamera(cam);
        camController->setQuat(cam.rotation);
        vector3 newPos = { cam.position.x, cam.position.y, cam.position.z };
        camController->setPos(newPos);
    }
}

void intercept::pre_start() {

    mainThreadID = GetCurrentThreadId();
    EnumWindows(EnumWindowsProc, 0); //Intercept cba stuff to get Arma hwnd

    if (ansel::isAnselAvailable()) {
        __debugbreak();



        ansel::Configuration config{};
        // Configure values that we want different from defaults:
        //config.translationalSpeedInWorldUnitsPerSecond = 5.0f;
        //config.right = { -axis_left.x,   -axis_left.y,   -axis_left.z };
        //config.up = { axis_up.x,      axis_up.y,      axis_up.z };
        //config.forward = { axis_forward.x, axis_forward.y, axis_forward.z };

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
            if (val) {
                sqf::hide_object(sqf::player(), true);
                hidingPlayerBody = true;
            } else {
                sqf::hide_object(sqf::player(), false);
                hidingPlayerBody = false;
            }
        };
        ansel::addUserControl(characterCheckbox);

    }



}

void intercept::pre_init() {

}

void intercept::post_init() {
    
}

void intercept::mission_ended() {

}
