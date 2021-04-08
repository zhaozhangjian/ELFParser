#include <map>
#include <vector>

struct KernParam {
  typedef enum {
    Value                  = 0,
    HiddenNone             = 1,
    HiddenGlobalOffsetX    = 2,
    HiddenGlobalOffsetY    = 3,
    HiddenGlobalOffsetZ    = 4,
    HiddenPrintfBuffer     = 5,
    HiddenDefaultQueue     = 6,
    HiddenCompletionAction = 7,
    MemoryObject           = 8,
    ReferenceObject        = 9,
    ValueObject            = 10,
    ImageObject            = 11,
    SamplerObject          = 12,
    QueueObject            = 13,
    HiddenMultiGridSync    = 14,
    HiddenHostcallBuffer   = 15,
  } value_type_t;

  size_t     _offset;
  size_t       _size;
  value_type_t _type;
};

struct KernMeta {
    unsigned int _kasz;
    unsigned int _vgpr;
    std::vector<KernParam> _params;
};

struct KernInfo {
    unsigned int _desc;
    unsigned int _desz;
    unsigned int _mach;
    unsigned int _masz;
    KernMeta     _meta;
};

const std::map<std::string, KernParam::value_type_t> ArgValueKindV3 = {
  {"by_value",                  KernParam::ValueObject},
  {"global_buffer",             KernParam::MemoryObject},
  {"dynamic_shared_pointer",    KernParam::MemoryObject},
  {"sampler",                   KernParam::SamplerObject},
  {"image",                     KernParam::ImageObject },
  {"pipe",                      KernParam::MemoryObject},
  {"queue",                     KernParam::QueueObject},
  {"hidden_global_offset_x",    KernParam::HiddenGlobalOffsetX},
  {"hidden_global_offset_y",    KernParam::HiddenGlobalOffsetY},
  {"hidden_global_offset_z",    KernParam::HiddenGlobalOffsetZ},
  {"hidden_none",               KernParam::HiddenNone},
  {"hidden_printf_buffer",      KernParam::HiddenPrintfBuffer},
  {"hidden_default_queue",      KernParam::HiddenDefaultQueue},
  {"hidden_completion_action",  KernParam::HiddenCompletionAction},
  {"hidden_multigrid_sync_arg", KernParam::HiddenMultiGridSync},
  {"hidden_hostcall_buffer",    KernParam::HiddenHostcallBuffer}
};