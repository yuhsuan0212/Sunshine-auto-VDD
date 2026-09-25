/** @file src/platform/windows/managed_vdd.cpp
 * @brief Exact-instance PnP control, durable ownership and topology restoration.
 */
#include "managed_vdd.h"

// clang-format off
// Win32 base types must precede the SetupAPI/Configuration Manager headers.
#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <regstr.h>
// clang-format on

#include <algorithm>
#include <devguid.h>
#include <display_device/windows/json.h>
#include <display_device/windows/win_api_layer.h>
#include <display_device/windows/win_display_device.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <objbase.h>
#include <thread>

namespace managed_vdd {
  namespace {
    using json = nlohmann::json;
    constexpr auto marker_name = L"SunshineManagedVddOwner";  ///< Device-local ownership proof.

    /** @brief Convert UTF-8 identifiers into Windows strings. */
    std::wstring wide(const std::string &value) {
      const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
      if (!count && !value.empty()) {
        throw std::runtime_error("Invalid UTF-8 identifier");
      }
      std::wstring out(count, L'\0');
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), out.data(), count);
      return out;
    }

    /** @brief Convert Windows identifiers into UTF-8. */
    std::string narrow(const std::wstring &value) {
      const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
      std::string out(count, '\0');
      WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
      return out;
    }

    /** @brief Load a bounded JSON document; malformed state is never treated as absent. */
    json read_json(const std::filesystem::path &path) {
      if (std::filesystem::file_size(path) > 1024 * 1024) {
        throw std::runtime_error("VDD state exceeds size limit");
      }
      std::ifstream input(path);
      return json::parse(input);
    }

    /** @brief Atomically replace a durable local file before changing devices. */
    void write_bytes(const std::filesystem::path &path, const std::string &data) {
      const auto tmp = std::filesystem::path(path.wstring() + L".tmp");
      const auto file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot create VDD journal");
      }
      DWORD written = 0;
      const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size() && FlushFileBuffers(file);
      CloseHandle(file);
      if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Cannot durably commit VDD journal");
      }
    }

    /** @brief Atomically replace a durable JSON journal. */
    void write_json(const std::filesystem::path &path, const json &value) {
      write_bytes(path, value.dump(2));
    }

    /** @brief Read a bounded VDD settings file without changing its encoding or layout. */
    std::string read_settings(const std::filesystem::path &path) {
      if (std::filesystem::file_size(path) > 512 * 1024) {
        throw std::runtime_error("VDD settings exceed size limit");
      }
      std::ifstream input(path, std::ios::binary);
      if (!input) {
        throw std::runtime_error("Cannot read VDD settings");
      }
      return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    /** @brief Own a SetupAPI device information set. */
    struct device_t {
      HDEVINFO set {SetupDiCreateDeviceInfoList(nullptr, nullptr)};  ///< Device information set.
      SP_DEVINFO_DATA info {sizeof(SP_DEVINFO_DATA)};  ///< Selected exact device.

      /** @brief Release the information set. */
      ~device_t() {
        if (set != INVALID_HANDLE_VALUE) {
          SetupDiDestroyDeviceInfoList(set);
        }
      }

      /** @brief Open an exact instance; never search by hardware ID alone. */
      bool open(const std::wstring &id) {
        return SetupDiOpenDeviceInfoW(set, id.c_str(), nullptr, 0, &info);
      }
    };

    /** @brief Query a devnode's unique instance identifier. */
    std::wstring instance_id(DEVINST devnode) {
      wchar_t buffer[MAX_DEVICE_ID_LEN] {};
      if (CM_Get_Device_IDW(devnode, buffer, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
        return {};
      }
      return buffer;
    }

    /** @brief Verify the exact device's hardware ID as well as its persistent owner token. */
    bool owned(device_t &device, const std::string &token) {
      wchar_t ids[4096] {};
      DWORD type = 0;
      if (!SetupDiGetDeviceRegistryPropertyW(device.set, &device.info, SPDRP_HARDWAREID, &type, reinterpret_cast<BYTE *>(ids), sizeof(ids) - sizeof(wchar_t), nullptr) || type != REG_MULTI_SZ) {
        return false;
      }
      bool matches = false;
      for (const auto *id = ids; *id; id += wcslen(id) + 1) {
        matches |= _wcsicmp(id, L"Root\\MttVDD") == 0;
      }
      if (!matches) {
        return false;
      }
      auto key = SetupDiOpenDevRegKey(device.set, &device.info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
      if (key == INVALID_HANDLE_VALUE) {
        return false;
      }
      wchar_t value[256] {};
      DWORD bytes = sizeof(value) - sizeof(wchar_t);
      const auto status = RegQueryValueExW(key, marker_name, nullptr, &type, reinterpret_cast<BYTE *>(value), &bytes);
      RegCloseKey(key);
      return status == ERROR_SUCCESS && type == REG_SZ && wide(token) == value;
    }

    /** @brief Check whether a display target is descended from the owned VDD device. */
    bool target_belongs_to(const DISPLAYCONFIG_PATH_INFO &path, const std::wstring &id) {
      DISPLAYCONFIG_TARGET_DEVICE_NAME name {};
      name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(name), path.targetInfo.adapterId, path.targetInfo.id};
      if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || !*name.monitorDevicePath) {
        return false;
      }
      device_t monitor;
      SP_DEVICE_INTERFACE_DATA iface {sizeof(iface)};
      if (!SetupDiOpenDeviceInterfaceW(monitor.set, name.monitorDevicePath, 0, &iface)) {
        return false;
      }
      DWORD size = 0;
      SetupDiGetDeviceInterfaceDetailW(monitor.set, &iface, nullptr, 0, &size, nullptr);
      std::vector<BYTE> buffer(size);
      if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        return false;
      }
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
      detail->cbSize = sizeof(*detail);
      if (!SetupDiGetDeviceInterfaceDetailW(monitor.set, &iface, detail, size, nullptr, &monitor.info)) {
        return false;
      }
      auto node = monitor.info.DevInst;
      for (int depth = 0; depth < 16; ++depth) {
        if (_wcsicmp(instance_id(node).c_str(), id.c_str()) == 0) {
          return true;
        }
        DEVINST parent;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) {
          break;
        }
        node = parent;
      }
      return false;
    }

    /** @brief Bound asynchronous device/display enumeration waits. */
    template<class Predicate>
    bool wait_for(Predicate predicate) {
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      do {
        try {
          if (predicate()) {
            return true;
          }
        } catch (const std::exception &) { /* PnP/CCD may be temporarily unavailable. */
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      } while (std::chrono::steady_clock::now() < end);
      return false;
    }
  }  // namespace

  /** @brief Windows state shared between the serialized manager and capture-name queries. */
  struct windows_backend_t::impl_t {
    HANDLE process_lock {nullptr};  ///< Prevents another process managing the same owned instance.
    std::filesystem::path journal;  ///< Baseline persisted independently from permanent ownership.
    std::filesystem::path settings_file {L"C:\\VirtualDisplayDriver\\vdd_settings.xml"};  ///< VDD XML read at each device activation.
    std::string id;  ///< Exact owned device instance.
    std::string token;  ///< Must match the marker on the devnode.
    std::shared_ptr<display_device::WinApiLayer> api {std::make_shared<display_device::WinApiLayer>()};  ///< CCD API wrapper.
    display_device::WinDisplayDevice displays {api};  ///< Stable monitor IDs and display operations.
    mutable std::mutex name_mutex;  ///< Protects selected display ID.
    std::string display_id;  ///< Capture target, refreshed after each activation.

    /** @brief Capture a baseline without changing devices, also used before provisioning. */
    json snapshot() const {
      const auto paths = api->queryDisplayConfig(display_device::QueryType::Active);
      if (!paths) {
        throw std::runtime_error("Cannot query original topology");
      }
      const auto topology = displays.getCurrentTopology();
      if (!paths->m_paths.empty() && topology.empty()) {
        throw std::runtime_error("Cannot identify original displays");
      }
      display_device::StringSet ids;
      for (const auto &group : topology) {
        ids.insert(group.begin(), group.end());
      }
      const auto modes = displays.getCurrentDisplayModes(ids);
      const auto hdr = displays.getCurrentHdrStates(ids);
      if (modes.size() != ids.size() || hdr.size() != ids.size()) {
        throw std::runtime_error("Cannot snapshot display modes/HDR");
      }
      json positions = json::object();
      std::string primary;
      for (const auto &device : displays.enumAvailableDevices()) {
        if (device.m_info && ids.contains(device.m_device_id)) {
          positions[device.m_device_id] = {device.m_info->m_origin_point.m_x, device.m_info->m_origin_point.m_y};
          if (device.m_info->m_primary) {
            primary = device.m_device_id;
          }
        }
      }
      if (positions.size() != ids.size() || (!ids.empty() && primary.empty())) {
        throw std::runtime_error("Cannot snapshot display positions/primary");
      }
      return {{"version", 1}, {"instance_id", id}, {"owner_token", token}, {"topology", display_device::toJson(topology)}, {"modes", display_device::toJson(modes)}, {"hdr", display_device::toJson(hdr)}, {"primary", primary}, {"positions", positions}};
    }

    /** @brief Release process ownership without changing the device. */
    ~impl_t() {
      if (process_lock) {
        ReleaseMutex(process_lock);
        CloseHandle(process_lock);
      }
    }

    /** @brief Validate that the configured instance remains the device provisioned by this tool. */
    void open_owned(device_t &device) const {
      if (!device.open(wide(id)) || !owned(device, token)) {
        throw std::runtime_error("Owned VDD instance missing or owner marker does not match");
      }
    }

    /** @brief Return disabled state, failing closed on inaccessible or ambiguous state. */
    bool disabled() const {
      device_t device;
      open_owned(device);
      ULONG status = 0, problem = 0;
      if (CM_Get_DevNode_Status(&status, &problem, device.info.DevInst, 0) != CR_SUCCESS) {
        throw std::runtime_error("Cannot query VDD devnode");
      }
      return (status & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED;
    }

    /** @brief Change only the owned instance and report deferred reboot accurately. */
    result_e toggle(bool enable) {
      device_t device;
      open_owned(device);
      if (!enable && disabled()) {
        return result_e::ok;
      }
      SP_PROPCHANGE_PARAMS params {};
      params.ClassInstallHeader = {sizeof(SP_CLASSINSTALL_HEADER), DIF_PROPERTYCHANGE};
      params.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
      params.Scope = DICS_FLAG_GLOBAL;
      if (!SetupDiSetClassInstallParamsW(device.set, &device.info, &params.ClassInstallHeader, sizeof(params)) || !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device.set, &device.info)) {
        return result_e::failed;
      }
      SP_DEVINSTALL_PARAMS_W install {sizeof(install)};
      if (!SetupDiGetDeviceInstallParamsW(device.set, &device.info, &install)) {
        return result_e::failed;
      }
      if (install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)) {
        return result_e::reboot_required;
      }
      return result_e::ok;
    }

    /** @brief Find only display targets whose PnP ancestor is our exact instance. */
    std::vector<std::string> targets(bool active_only) const {
      const auto paths = api->queryDisplayConfig(active_only ? display_device::QueryType::Active : display_device::QueryType::All);
      if (!paths) {
        throw std::runtime_error("Display enumeration failed; not assuming headless");
      }
      std::vector<std::string> out;
      for (const auto &path : paths->m_paths) {
        if (target_belongs_to(path, wide(id))) {
          auto target = api->getDeviceId(path);
          if (!target.empty() && std::find(out.begin(), out.end(), target) == out.end()) {
            out.push_back(target);
          }
        }
      }
      return out;
    }

    /** @brief Confirm that Windows advertises the owned target's requested mode. */
    bool mode_available(mode_t requested) const {
      const auto paths = api->queryDisplayConfig(display_device::QueryType::Active);
      if (!paths) {
        return false;
      }
      for (const auto &path : paths->m_paths) {
        if (!target_belongs_to(path, wide(id))) {
          continue;
        }
        const auto display_name = wide(api->getDisplayName(path));
        if (display_name.empty()) {
          continue;
        }
        for (DWORD index = 0;; ++index) {
          DEVMODEW mode {};
          mode.dmSize = sizeof(mode);
          if (!EnumDisplaySettingsExW(display_name.c_str(), index, &mode, 0)) {
            break;
          }
          if (mode.dmPelsWidth == static_cast<DWORD>(requested.width) && mode.dmPelsHeight == static_cast<DWORD>(requested.height) && mode.dmDisplayFrequency == static_cast<DWORD>(requested.fps)) {
            return true;
          }
        }
      }
      return false;
    }
  };

  windows_backend_t::windows_backend_t(const std::filesystem::path &owner_file):
      impl_(std::make_unique<impl_t>()) {
    const auto owner = read_json(owner_file);
    if (owner.at("version") != 1) {
      throw std::runtime_error("Unsupported VDD ownership version");
    }
    impl_->id = owner.at("instance_id").get<std::string>();
    impl_->token = owner.at("owner_token").get<std::string>();
    if (impl_->token.empty()) {
      throw std::runtime_error("Empty VDD owner token");
    }
    auto lock = CreateMutexW(nullptr, FALSE, (L"Global\\SunshineManagedVdd-" + wide(impl_->token)).c_str());
    if (!lock) {
      throw std::runtime_error("Cannot open VDD process lock");
    }
    const auto acquired = WaitForSingleObject(lock, 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
      CloseHandle(lock);
      throw std::runtime_error("Another process owns this managed VDD");
    }
    impl_->process_lock = lock;
    impl_->journal = owner_file.wstring() + L".baseline.json";
    HKEY settings_key {};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver", 0, KEY_QUERY_VALUE, &settings_key) == ERROR_SUCCESS) {
      wchar_t folder[MAX_PATH] {};
      DWORD type = 0;
      DWORD bytes = sizeof(folder) - sizeof(wchar_t);
      const auto status = RegQueryValueExW(settings_key, L"VDDPATH", nullptr, &type, reinterpret_cast<BYTE *>(folder), &bytes);
      RegCloseKey(settings_key);
      if (status == ERROR_SUCCESS && type == REG_SZ && folder[0]) {
        impl_->settings_file = std::filesystem::path(folder) / L"vdd_settings.xml";
      }
    }
    device_t device;
    impl_->open_owned(device);
  }

  windows_backend_t::~windows_backend_t() = default;

  bool windows_backend_t::has_checkpoint() {
    return std::filesystem::exists(impl_->journal);
  }

  bool windows_backend_t::checkpoint() {
    if (has_checkpoint() || !impl_->disabled() || !impl_->targets(true).empty()) {
      return false;
    }
    write_json(impl_->journal, impl_->snapshot());
    return true;
  }

  bool windows_backend_t::prepare_mode(mode_t mode) {
    const auto original = read_settings(impl_->settings_file);
    const auto modified = ensure_mode(original, mode);
    if (!modified) {
      return false;
    }
    if (*modified == original) {
      return true;
    }
    auto saved = read_json(impl_->journal);
    if (saved.at("instance_id") != impl_->id || saved.at("owner_token") != impl_->token) {
      return false;
    }
    saved["vdd_xml_path"] = narrow(impl_->settings_file.wstring());
    saved["vdd_xml_original"] = original;
    saved["vdd_xml_managed"] = *modified;
    write_json(impl_->journal, saved);
    write_bytes(impl_->settings_file, *modified);
    return true;
  }

  result_e windows_backend_t::activate() {
    return impl_->toggle(true);
  }

  bool windows_backend_t::wait_ready() {
    return wait_for([&] {
      const auto targets = impl_->targets(false);
      if (targets.size() != 1 || impl_->disabled()) {
        return false;
      }
      std::lock_guard lock(impl_->name_mutex);
      impl_->display_id = targets.front();
      return true;
    });
  }

  bool windows_backend_t::restore() {
    device_t device;
    impl_->open_owned(device);
    const auto saved = read_json(impl_->journal);
    if (saved.at("version") != 1 || saved.at("instance_id") != impl_->id || saved.at("owner_token") != impl_->token) {
      return false;
    }
    display_device::ActiveTopology topology;
    display_device::DeviceDisplayModeMap modes;
    display_device::HdrStateMap hdr;
    if (!display_device::fromJson(saved.at("topology").get<std::string>(), topology) || !display_device::fromJson(saved.at("modes").get<std::string>(), modes) || !display_device::fromJson(saved.at("hdr").get<std::string>(), hdr)) {
      return false;
    }
    if (topology.empty()) {
      // Zero-output state is achieved by disabling our device, never by a zero-path SetDisplayConfig.
      return modes.empty() && hdr.empty() && saved.at("primary") == "" && saved.at("positions").empty();
    }
    if (!impl_->displays.setTopology(topology) || !impl_->displays.setDisplayModes(modes) || !impl_->displays.setAsPrimary(saved.at("primary").get<std::string>()) || !impl_->displays.setHdrStates(hdr)) {
      return false;
    }
    auto paths = impl_->api->queryDisplayConfig(display_device::QueryType::Active);
    if (!paths) {
      return false;
    }
    for (const auto &path : paths->m_paths) {
      const auto id = impl_->api->getDeviceId(path);
      const auto index = path.sourceInfo.sourceModeInfoIdx;
      if (!saved.at("positions").contains(id) || index >= paths->m_modes.size() || paths->m_modes[index].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
        return false;
      }
      auto &position = paths->m_modes[index].sourceMode.position;
      position.x = saved.at("positions").at(id).at(0).get<LONG>();
      position.y = saved.at("positions").at(id).at(1).get<LONG>();
    }
    if (impl_->api->setDisplayConfig(paths->m_paths, paths->m_modes, SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE | SDC_VIRTUAL_MODE_AWARE) != ERROR_SUCCESS) {
      return false;
    }
    return wait_for([&] {
      if (!impl_->displays.isTopologyTheSame(impl_->displays.getCurrentTopology(), topology) || !impl_->displays.isPrimary(saved.at("primary").get<std::string>())) {
        return false;
      }
      display_device::StringSet ids;
      for (const auto &[id, mode] : modes) {
        ids.insert(id);
      }
      if (impl_->displays.getCurrentDisplayModes(ids) != modes || impl_->displays.getCurrentHdrStates(ids) != hdr) {
        return false;
      }
      std::size_t matched = 0;
      for (const auto &entry : impl_->displays.enumAvailableDevices()) {
        if (entry.m_info && ids.contains(entry.m_device_id)) {
          const auto &position = saved.at("positions").at(entry.m_device_id);
          if (entry.m_info->m_origin_point.m_x != position.at(0) || entry.m_info->m_origin_point.m_y != position.at(1)) {
            return false;
          }
          ++matched;
        }
      }
      return matched == ids.size();
    });
  }

  result_e windows_backend_t::deactivate() {
    return impl_->toggle(false);
  }

  bool windows_backend_t::wait_inactive() {
    return wait_for([&] {
      return impl_->disabled() && impl_->targets(true).empty();
    });
  }

  bool windows_backend_t::clear_checkpoint() {
    if (has_checkpoint()) {
      const auto saved = read_json(impl_->journal);
      if (saved.contains("vdd_xml_managed")) {
        const auto xml_path = std::filesystem::path(wide(saved.at("vdd_xml_path").get<std::string>()));
        if (std::filesystem::exists(xml_path)) {
          const auto current = read_settings(xml_path);
          if (current == saved.at("vdd_xml_managed").get<std::string>()) {
            write_bytes(xml_path, saved.at("vdd_xml_original").get<std::string>());
          }
        }
      }
    }
    std::lock_guard lock(impl_->name_mutex);
    impl_->display_id.clear();
    return !has_checkpoint() || std::filesystem::remove(impl_->journal);
  }

  std::string windows_backend_t::device_id() const {
    std::lock_guard lock(impl_->name_mutex);
    return impl_->display_id;
  }

  bool windows_backend_t::configure(int width, int height, int fps, bool hdr) {
    const auto id = device_id();
    if (id.empty() || width <= 0 || height <= 0 || fps <= 0) {
      return false;
    }
    auto topology = impl_->displays.getCurrentTopology();
    bool active = false;
    for (const auto &group : topology) {
      active |= std::find(group.begin(), group.end(), id) != group.end();
    }
    if (!active) {
      topology.push_back({id});
    }
    const display_device::DeviceDisplayModeMap modes {{id, {{static_cast<unsigned>(width), static_cast<unsigned>(height)}, {static_cast<unsigned>(fps), 1}}}};
    if (!impl_->displays.setTopology(topology) || !wait_for([&] {
          return impl_->mode_available({width, height, fps});
        }) ||
        !impl_->displays.setDisplayModes(modes) || !impl_->displays.setAsPrimary(id)) {
      return false;
    }
    const auto states = impl_->displays.getCurrentHdrStates({id});
    if (!states.contains(id)) {
      return false;
    }
    if (states.at(id)) {
      if (!impl_->displays.setHdrStates({{id, hdr ? display_device::HdrState::Enabled : display_device::HdrState::Disabled}})) {
        return false;
      }
    } else if (hdr) {
      return false;
    }
    return wait_for([&] {
      return impl_->displays.isPrimary(id) && impl_->displays.getCurrentDisplayModes({id}) == modes;
    });
  }

  std::string windows_backend_t::inspect() const {
    return json {{"instance_id", impl_->id}, {"disabled", impl_->disabled()}, {"active_targets", impl_->targets(true)}, {"checkpoint", std::filesystem::exists(impl_->journal)}}.dump(2);
  }

  std::string windows_backend_t::provision(const std::filesystem::path &inf, const std::filesystem::path &owner_file) {
    if (std::filesystem::exists(owner_file)) {
      throw std::runtime_error("Ownership file already exists; refusing a duplicate device");
    }
    impl_t original;
    auto baseline = original.snapshot();
    // Only stage the selected signed package; this does not update other matching instances.
    wchar_t published[MAX_PATH] {};
    if (!SetupCopyOEMInfW(std::filesystem::absolute(inf).c_str(), nullptr, SPOST_PATH, 0, published, MAX_PATH, nullptr, nullptr)) {
      throw std::runtime_error("Could not stage signed VDD INF");
    }
    GUID cls;
    wchar_t class_name[256] {};
    if (!SetupDiGetINFClassW(published, &cls, class_name, 256, nullptr) || cls != GUID_DEVCLASS_DISPLAY) {
      throw std::runtime_error("INF is not a display driver");
    }
    device_t device;
    if (!SetupDiCreateDeviceInfoW(device.set, class_name, &cls, L"Sunshine managed VDD (prototype)", nullptr, DICD_GENERATE_ID, &device.info)) {
      throw std::runtime_error("Cannot create VDD device information");
    }
    const wchar_t hardware_id[] = L"Root\\MttVDD\0";
    const DWORD disabled_flag = CONFIGFLAG_DISABLED;
    if (!SetupDiSetDeviceRegistryPropertyW(device.set, &device.info, SPDRP_HARDWAREID, reinterpret_cast<const BYTE *>(hardware_id), sizeof(hardware_id)) || !SetupDiSetDeviceRegistryPropertyW(device.set, &device.info, SPDRP_CONFIGFLAGS, reinterpret_cast<const BYTE *>(&disabled_flag), sizeof(disabled_flag)) || !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, device.set, &device.info)) {
      throw std::runtime_error("Cannot register disabled VDD devnode");
    }
    const auto id = narrow(instance_id(device.info.DevInst));
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) {
      throw std::runtime_error("Cannot generate ownership token for " + id);
    }
    wchar_t token[40] {};
    StringFromGUID2(guid, token, 40);
    // Persist exact identity before installation so an interrupted provisioning operation is diagnosable.
    write_json(owner_file, {{"version", 1}, {"instance_id", id}, {"owner_token", narrow(token)}, {"published_inf", narrow(published)}});
    baseline["instance_id"] = id;
    baseline["owner_token"] = narrow(token);
    write_json(owner_file.wstring() + L".baseline.json", baseline);
    auto key = SetupDiCreateDevRegKeyW(device.set, &device.info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, nullptr, nullptr);
    if (key == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("Cannot mark new VDD instance " + id);
    }
    const auto marked = RegSetValueExW(key, marker_name, 0, REG_SZ, reinterpret_cast<const BYTE *>(token), static_cast<DWORD>((wcslen(token) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (marked != ERROR_SUCCESS) {
      throw std::runtime_error("Cannot save VDD owner marker");
    }
    SP_DEVINSTALL_PARAMS_W params {sizeof(params)};
    if (!SetupDiGetDeviceInstallParamsW(device.set, &device.info, &params)) {
      throw std::runtime_error("Cannot read install parameters");
    }
    params.Flags |= DI_ENUMSINGLEINF;
    wcscpy_s(params.DriverPath, published);
    SP_DRVINFO_DATA_W driver {sizeof(driver)};
    if (!SetupDiSetDeviceInstallParamsW(device.set, &device.info, &params) || !SetupDiBuildDriverInfoList(device.set, &device.info, SPDIT_COMPATDRIVER) || !SetupDiEnumDriverInfoW(device.set, &device.info, SPDIT_COMPATDRIVER, 0, &driver) || !SetupDiSetSelectedDriverW(device.set, &device.info, &driver) || !SetupDiCallClassInstaller(DIF_INSTALLDEVICE, device.set, &device.info)) {
      throw std::runtime_error("Cannot bind selected INF to owned device " + id);
    }
    windows_backend_t backend(owner_file);
    if (!backend.restore()) {
      throw std::runtime_error("Provisioned VDD; baseline restoration requires recovery: " + id);
    }
    const auto result = backend.deactivate();
    if (result != result_e::ok || !backend.wait_inactive()) {
      throw std::runtime_error("Provisioned device requires recovery/reboot: " + id);
    }
    if (!backend.clear_checkpoint()) {
      throw std::runtime_error("Cannot clear provisioning checkpoint");
    }
    return id;
  }
}  // namespace managed_vdd
