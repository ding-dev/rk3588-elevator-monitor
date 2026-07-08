
- `backend_cpp/`：边缘端 C++ 主控、协议解析、UI 调度、媒体管理、摄像头推流、RKNN 检测、Unix Socket 通信。
- `frontend_py/`：MQTT 云端桥接脚本、Flask 广告上传服务、网页监控端。
- `lv_port_pc_vscode/lvgl_ui/`：SquareLine/LVGL 生成的本项目界面代码。
- `lv_port_pc_vscode/src/`、`config/`、`lv_conf.h`：本项目 LVGL/SDL/HAL 相关代码和配置。
- `lv_port_pc_vscode/lvgl/src/`：LVGL 核心源码，供 `backend_cpp/CMakeLists.txt` 编译 UI 使用。
- `lv_port_pc_vscode/lvgl/env_support/cmake/`：LVGL CMake 构建所需脚本。
- `vedio_ai/`：RKNN 模型、RKNN runtime 头文件/库、OpenCV 头文件/库。
- `scripts/`：交叉编译、部署、sysroot 同步脚本。

