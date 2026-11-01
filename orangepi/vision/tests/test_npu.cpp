// Requiere /dev/vipcore: solo corre en la placa.
#include <filesystem>
#include <fstream>
#include <iterator>

#include "minitest.hpp"
#include "vision/npu.hpp"

TEST(network_with_missing_file_throws_npu_error) {
  vision::npu::Driver driver;
  CHECK_THROWS(vision::npu::Network(driver, "/nonexistent/dir/model.nb"), vision::npu::NpuError);
}

TEST(network_with_non_nbg_file_throws_npu_error) {
  vision::npu::Driver driver;
  CHECK_THROWS(vision::npu::Network(driver, "/etc/hostname"), vision::npu::NpuError);
}

TEST(driver_can_be_created_again_after_destroy) {
  { vision::npu::Driver first; }
  vision::npu::Driver second;
  CHECK(second.version() != 0);
}

TEST(network_rejects_zero_timeout) {
  vision::npu::Driver driver;
  CHECK_THROWS(vision::npu::Network(driver, "/nonexistent/dir/model.nb", vision::npu::NetworkOptions{0}),
               std::invalid_argument);
}

// Con la red de ejemplo del fabricante: timeout configurado, map/unmap y tamano logico.
TEST(vendor_network_runs_with_custom_timeout_and_reports_logical_size) {
  const char* model = "/opt/vpm_run/network_binary.nb";
  const char* input_file = "/opt/vpm_run/input_0.dat";
  if (!std::filesystem::exists(model) || !std::filesystem::exists(input_file)) return;

  vision::npu::Driver driver;
  vision::npu::Network network(driver, model, vision::npu::NetworkOptions{500});
  const vision::TensorInfo& input = network.input_info(0);
  CHECK_EQ(input.element_count, std::size_t{224} * 224 * 3);
  CHECK(input.size_bytes >= input.element_count);

  std::ifstream file(input_file, std::ios::binary);
  std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const std::vector<std::uint8_t> data(chars.begin(), chars.end());
  network.set_input(0, data);
  network.run();
  const vision::TensorInfo& output = network.output_info(0);
  CHECK_EQ(output.element_count, std::size_t{2});
  CHECK_EQ(network.read_output(0).size(), output.element_count);
  CHECK_EQ(network.read_output_raw(0).size(), output.size_bytes);
  CHECK(!output.name.empty());
}
