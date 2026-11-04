#include "vision/npu.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>

#include <vip_lite.h>

namespace vision::npu {

namespace {

using SteadyClock = std::chrono::steady_clock;

double elapsed_ms(SteadyClock::time_point since) {
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - since).count();
}

const char* status_name(vip_status_e status) {
  switch (status) {
    case VIP_SUCCESS: return "VIP_SUCCESS";
    case VIP_ERROR_TIMEOUT: return "VIP_ERROR_TIMEOUT";
    case VIP_ERROR_IO: return "VIP_ERROR_IO";
    case VIP_ERROR_INVALID_ARGUMENTS: return "VIP_ERROR_INVALID_ARGUMENTS";
    case VIP_ERROR_NOT_SUPPORTED: return "VIP_ERROR_NOT_SUPPORTED";
    case VIP_ERROR_OUT_OF_RESOURCE: return "VIP_ERROR_OUT_OF_RESOURCE";
    case VIP_ERROR_OUT_OF_MEMORY: return "VIP_ERROR_OUT_OF_MEMORY";
    case VIP_ERROR_INVALID_NETWORK: return "VIP_ERROR_INVALID_NETWORK";
    case VIP_ERROR_MISSING_INPUT_OUTPUT: return "VIP_ERROR_MISSING_INPUT_OUTPUT";
    case VIP_ERROR_NETWORK_NOT_PREPARED: return "VIP_ERROR_NETWORK_NOT_PREPARED";
    case VIP_ERROR_NETWORK_INCOMPATIBLE: return "VIP_ERROR_NETWORK_INCOMPATIBLE";
    case VIP_ERROR_FAILURE: return "VIP_ERROR_FAILURE";
    case VIP_ERROR_POWER_OFF: return "VIP_ERROR_POWER_OFF";
    case VIP_ERROR_POWER_STOP: return "VIP_ERROR_POWER_STOP";
    case VIP_ERROR_RECOVERY: return "VIP_ERROR_RECOVERY";
    case VIP_ERROR_CANCELED: return "VIP_ERROR_CANCELED";
    case VIP_ERROR_NAN_INF: return "VIP_ERROR_NAN_INF";
    case VIP_ERROR_FUSA: return "VIP_ERROR_FUSA";
  }
  return "VIP_ERROR_UNKNOWN";
}

void check(vip_status_e status, const char* call) {
  if (status != VIP_SUCCESS) {
    throw NpuError(std::string(call) + " fallo: " + status_name(status) + " (" +
                   std::to_string(static_cast<int>(status)) + ")");
  }
}

DataFormat to_data_format(vip_enum format) {
  switch (format) {
    case VIP_BUFFER_FORMAT_FP32: return DataFormat::Fp32;
    case VIP_BUFFER_FORMAT_FP16: return DataFormat::Fp16;
    case VIP_BUFFER_FORMAT_UINT8: return DataFormat::Uint8;
    case VIP_BUFFER_FORMAT_INT8: return DataFormat::Int8;
    case VIP_BUFFER_FORMAT_UINT16: return DataFormat::Uint16;
    case VIP_BUFFER_FORMAT_INT16: return DataFormat::Int16;
    default: return DataFormat::Other;
  }
}

QuantFormat to_quant_format(vip_enum quant) {
  switch (quant) {
    case VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT: return QuantFormat::DynamicFixedPoint;
    case VIP_BUFFER_QUANTIZE_TF_ASYMM: return QuantFormat::Affine;
    default: return QuantFormat::None;
  }
}

constexpr std::size_t kMaxTensorElements = std::size_t{64} << 20;
// Techo del buffer que reserva el driver (64 MB): el .nb manda en las dimensiones, asi
// que sin este techo un .nb manipulado podria pedir gigabytes en la placa.
constexpr std::size_t kMaxTensorBytes = std::size_t{64} << 20;

// vip_query_input y vip_query_output comparten firma.
using QueryTensorFn = vip_status_e (*)(vip_network, vip_uint32_t, vip_enum, void*);

struct Tensor {
  TensorInfo info;
  vip_buffer_create_params_t params{};
  vip_buffer buffer = nullptr;
};

// Lee las propiedades del tensor `index` y arma los parametros para crear su buffer.
Tensor query_tensor(QueryTensorFn query, vip_network network, vip_uint32_t index) {
  Tensor t;
  vip_uint32_t num_dims = 0;
  check(query(network, index, VIP_BUFFER_PROP_NUM_OF_DIMENSION, &num_dims), "query num_of_dimension");
  if (num_dims == 0 || num_dims > 6) {
    throw NpuError("tensor " + std::to_string(index) + " con " + std::to_string(num_dims) +
                   " dimensiones (soportado: 1 a 6)");
  }
  vip_uint32_t sizes[6] = {};
  check(query(network, index, VIP_BUFFER_PROP_SIZES_OF_DIMENSION, sizes), "query sizes_of_dimension");
  vip_enum data_format = 0;
  check(query(network, index, VIP_BUFFER_PROP_DATA_FORMAT, &data_format), "query data_format");
  vip_enum quant_format = 0;
  check(query(network, index, VIP_BUFFER_PROP_QUANT_FORMAT, &quant_format), "query quant_format");
  vip_char_t name[64] = {};
  check(query(network, index, VIP_BUFFER_PROP_NAME, name), "query name");

  // Elementos logicos del tensor; las dimensiones vienen del .nb, asi que se acotan.
  // El techo es el del buffer: 64 M de elementos son 256 MB en float, de sobra para un
  // modelo de rostro y mas que suficiente para que un .nb manipulado no pida gigabytes
  // en una placa de 4 GB.
  std::size_t element_count = 1;
  for (vip_uint32_t d = 0; d < num_dims; ++d) {
    element_count *= sizes[d];
    if (sizes[d] == 0 || element_count > kMaxTensorElements) {
      throw NpuError("tensor " + std::to_string(index) + " con dimensiones fuera de rango");
    }
  }

  t.params.num_of_dims = num_dims;
  std::memcpy(t.params.sizes, sizes, sizeof(sizes));
  t.params.data_format = data_format;
  t.params.quant_format = quant_format;
  t.params.memory_type = VIP_BUFFER_MEMORY_TYPE_DEFAULT;

  t.info.name.assign(name, strnlen(name, sizeof name));
  t.info.element_count = element_count;
  t.info.dims.assign(sizes, sizes + num_dims);
  t.info.format = to_data_format(data_format);
  t.info.quant = to_quant_format(quant_format);

  if (quant_format == VIP_BUFFER_QUANTIZE_DYNAMIC_FIXED_POINT) {
    vip_uint8_t pos = 0;
    check(query(network, index, VIP_BUFFER_PROP_FIXED_POINT_POS, &pos), "query fixed_point_pos");
    t.params.quant_data.dfp.fixed_point_pos = pos;
    t.info.fixed_point_pos = pos;
  } else if (quant_format == VIP_BUFFER_QUANTIZE_TF_ASYMM) {
    vip_float_t scale = 1.0f;
    vip_uint32_t zero_point = 0;
    check(query(network, index, VIP_BUFFER_PROP_TF_SCALE, &scale), "query tf_scale");
    check(query(network, index, VIP_BUFFER_PROP_TF_ZERO_POINT, &zero_point), "query tf_zero_point");
    t.params.quant_data.affine.scale = scale;
    t.params.quant_data.affine.zeroPoint = static_cast<vip_int32_t>(zero_point);
    t.info.scale = scale;
    t.info.zero_point = static_cast<std::int32_t>(zero_point);
  }
  return t;
}

void create_buffer(Tensor& t) {
  // Techo del buffer que reserva el driver: las dimensiones del .nb mandan, asi que un
  // .nb manipulado podria pedir gigabytes en una placa de 4 GB. Se comprueba ANTES de
  // reservar, que es cuando todavia se puede fallar con un mensaje claro.
  const std::size_t logical_bytes = t.info.element_count * element_size(t.info.format);
  if (logical_bytes > kMaxTensorBytes) {
    throw NpuError("el tensor necesita " + std::to_string(logical_bytes) +
                   " bytes y el techo es " + std::to_string(kMaxTensorBytes));
  }
  check(vip_create_buffer(&t.params, sizeof(t.params), &t.buffer), "vip_create_buffer");
  t.info.size_bytes = vip_get_buffer_size(t.buffer);
  if (t.info.size_bytes < logical_bytes) {
    throw NpuError("el driver reservo " + std::to_string(t.info.size_bytes) + " bytes para un tensor de " +
                   std::to_string(logical_bytes) + " bytes logicos");
  }
}

// Mapea un buffer y garantiza el unmap aunque algo lance mientras esta mapeado.
class MappedBuffer {
 public:
  MappedBuffer(vip_buffer buffer, const char* what) : buffer_(buffer), data_(vip_map_buffer(buffer)) {
    if (data_ == nullptr) throw NpuError(std::string("vip_map_buffer devolvio NULL en ") + what);
  }
  MappedBuffer(const MappedBuffer&) = delete;
  MappedBuffer& operator=(const MappedBuffer&) = delete;
  ~MappedBuffer() {
    if (data_ != nullptr) vip_unmap_buffer(buffer_);
  }

  void* data() const { return data_; }

  // Camino normal: desmapea y comprueba el estado.
  void unmap() {
    data_ = nullptr;
    check(vip_unmap_buffer(buffer_), "vip_unmap_buffer");
  }

 private:
  vip_buffer buffer_;
  void* data_;
};

}  // namespace

Driver::Driver() {
  check(vip_init(), "vip_init");
}

Driver::~Driver() {
  vip_destroy();
}

std::uint32_t Driver::version() const {
  return vip_get_version();
}

struct Network::Impl {
  vip_network network = nullptr;
  std::vector<Tensor> inputs;
  std::vector<Tensor> outputs;
  Timings timings;

  ~Impl() {
    if (network != nullptr) {
      vip_finish_network(network);
      vip_destroy_network(network);
    }
    for (Tensor& t : inputs) {
      if (t.buffer != nullptr) vip_destroy_buffer(t.buffer);
    }
    for (Tensor& t : outputs) {
      if (t.buffer != nullptr) vip_destroy_buffer(t.buffer);
    }
  }

  void load(const std::filesystem::path& nb_file, const NetworkOptions& options) {
    auto start = SteadyClock::now();
    const std::string path = nb_file.string();
    check(vip_create_network(path.c_str(), 0, VIP_CREATE_NETWORK_FROM_FILE, &network),
          "vip_create_network");
    timings.create_ms = elapsed_ms(start);

    start = SteadyClock::now();
    vip_uint32_t input_count = 0;
    vip_uint32_t output_count = 0;
    check(vip_query_network(network, VIP_NETWORK_PROP_INPUT_COUNT, &input_count), "query input_count");
    check(vip_query_network(network, VIP_NETWORK_PROP_OUTPUT_COUNT, &output_count), "query output_count");
    for (vip_uint32_t i = 0; i < input_count; ++i) {
      inputs.push_back(query_tensor(vip_query_input, network, i));
    }
    for (vip_uint32_t i = 0; i < output_count; ++i) {
      outputs.push_back(query_tensor(vip_query_output, network, i));
    }

    // Las propiedades de la red se fijan antes de prepararla.
    vip_uint32_t timeout_ms = options.timeout_ms;
    check(vip_set_network(network, VIP_NETWORK_PROP_SET_TIME_OUT, &timeout_ms), "set time_out");
    check(vip_prepare_network(network), "vip_prepare_network");
    for (vip_uint32_t i = 0; i < input_count; ++i) {
      create_buffer(inputs[i]);
      check(vip_set_input(network, i, inputs[i].buffer), "vip_set_input");
    }
    for (vip_uint32_t i = 0; i < output_count; ++i) {
      create_buffer(outputs[i]);
      check(vip_set_output(network, i, outputs[i].buffer), "vip_set_output");
    }
    timings.prepare_ms = elapsed_ms(start);
  }
};

Network::Network(const Driver& /*driver*/, const std::filesystem::path& nb_file,
                 const NetworkOptions& options)
    : impl_(std::make_unique<Impl>()) {
  if (options.timeout_ms == 0) {
    throw std::invalid_argument("NetworkOptions.timeout_ms debe ser > 0");
  }
  if (!std::filesystem::is_regular_file(nb_file)) {
    throw NpuError("modelo no encontrado: " + nb_file.string());
  }
  impl_->load(nb_file, options);
}

Network::~Network() = default;

std::size_t Network::input_count() const { return impl_->inputs.size(); }
std::size_t Network::output_count() const { return impl_->outputs.size(); }

const TensorInfo& Network::input_info(std::size_t index) const {
  return impl_->inputs.at(index).info;
}

const TensorInfo& Network::output_info(std::size_t index) const {
  return impl_->outputs.at(index).info;
}

void Network::set_input(std::size_t index, std::span<const std::uint8_t> data) {
  const Tensor& t = impl_->inputs.at(index);
  if (data.size() != t.info.size_bytes) {
    throw NpuError("entrada " + std::to_string(index) + ": se esperaban " +
                   std::to_string(t.info.size_bytes) + " bytes y llegaron " +
                   std::to_string(data.size()));
  }
  const auto start = SteadyClock::now();
  MappedBuffer mapped(t.buffer, ("la entrada " + std::to_string(index)).c_str());
  std::memcpy(mapped.data(), data.data(), data.size());
  check(vip_flush_buffer(t.buffer, VIP_BUFFER_OPER_TYPE_FLUSH), "vip_flush_buffer");
  mapped.unmap();
  impl_->timings.input_copy_ms = elapsed_ms(start);
}

void Network::run() {
  const auto start = SteadyClock::now();
  check(vip_run_network(impl_->network), "vip_run_network");
  impl_->timings.run_ms = elapsed_ms(start);
}

std::vector<std::uint8_t> Network::read_output_raw(std::size_t index) {
  const Tensor& t = impl_->outputs.at(index);
  const auto start = SteadyClock::now();
  check(vip_flush_buffer(t.buffer, VIP_BUFFER_OPER_TYPE_INVALIDATE), "vip_flush_buffer");
  MappedBuffer mapped(t.buffer, ("la salida " + std::to_string(index)).c_str());
  const auto* bytes = static_cast<const std::uint8_t*>(mapped.data());
  std::vector<std::uint8_t> out(bytes, bytes + t.info.size_bytes);
  mapped.unmap();
  impl_->timings.output_read_ms = elapsed_ms(start);
  return out;
}

std::vector<float> Network::read_output(std::size_t index) {
  const std::vector<std::uint8_t> raw = read_output_raw(index);
  return dequantize(impl_->outputs.at(index).info, raw);
}

const Timings& Network::timings() const { return impl_->timings; }

}  // namespace vision::npu
