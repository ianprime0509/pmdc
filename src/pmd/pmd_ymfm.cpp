#include "pmd_ymfm.h"

// PMD's feature flags collide with third-party identifiers: `pcm` is a YMFM
// member name and `sync` is a libc++ member name. The one value used below is
// saved before the headers are included.
#if pcm
#define PMD_YMFM_PCM 1
#else
#define PMD_YMFM_PCM 0
#endif
#undef pcm
#undef sync
#undef vsync

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "ymfm_opn.h"

namespace {

// The rhythm ROM is a file the host loads, so this warning is backend state
// rather than one of the driver's translated messages.
#if lang == 256 * 'e' + 'n'
constexpr char rom_missing[] =
    "\aWARNING:ym2608_adpcm_rom.bin not found. "
    "The rhythm samples are silent.\n";
#else
constexpr char rom_missing[] =
    "\aWARNING:ym2608_adpcm_rom.bin が見つかりません．"
    "RHYTHM音源は発音しません．\n";
#endif

constexpr uint32_t ym2203_clock = 3993600;
constexpr uint32_t ym2608_clock = 7987200;
// The YM2608's ADPCM sample memories. ADPCM-A addresses are 16 bits wide; the
// ADPCM-B start/end registers address the DRAM through a shift of up to 2.
constexpr uint32_t adpcm_a_size = 0x10000;
constexpr uint32_t adpcm_b_size = 0x40000;

// A chip of any OPN type, so the backend does not carry three copies of the
// port and timer logic.
struct chip_interface {
    virtual ~chip_interface() = default;
    virtual void reset() = 0;
    virtual uint8_t read(uint32_t offset) = 0;
    virtual void write(uint32_t offset, uint8_t data) = 0;
    virtual void generate(int32_t *out) = 0;
    virtual uint32_t sample_rate(uint32_t clock) = 0;
};

template <typename Chip>
struct chip_model final : chip_interface {
    explicit chip_model(ymfm::ymfm_interface &intf) : chip(intf) {
        // MIN is the lowest rate YMFM offers, which for the OPN family is
        // three times the FM rate. MAX would repeat every FM sample 18 times
        // for no audible gain and an 18x larger buffer for the host.
        chip.set_fidelity(ymfm::OPN_FIDELITY_MIN);
    }

    void reset() override { chip.reset(); }

    uint8_t read(uint32_t offset) override { return chip.read(offset); }

    void write(uint32_t offset, uint8_t data) override {
        chip.write(offset, data);
    }

    void generate(int32_t *out) override {
        typename Chip::output_data sample;
        chip.generate(&sample);
        // The OPNA family puts FM on two outputs and SSG on a third; the OPN
        // chips are mono, so their FM output feeds both sides.
        int32_t left = sample.data[0];
        int32_t right = sample.data[Chip::FM_OUTPUTS == 2 ? 1 : 0];
        int32_t ssg = sample.data[Chip::OUTPUTS - 1];
        out[0] = left + ssg;
        out[1] = right + ssg;
    }

    uint32_t sample_rate(uint32_t clock) override {
        return chip.sample_rate(clock);
    }

    Chip chip;
};

} // namespace

struct pmd_ymfm : ymfm::ymfm_interface {
    explicit pmd_ymfm(const struct pmd_ymfm_config &config);

    // ymfm_interface
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    uint8_t ymfm_external_read(ymfm::access_class type,
                               uint32_t address) override;
    void ymfm_external_write(ymfm::access_class type, uint32_t address,
                             uint8_t data) override;

    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t value);
    void generate(uint32_t samples, int32_t *out);

    // Port accesses advance the emulated clock without processing timers: the
    // driver's BUSY handshake needs time to pass, but a timer interrupt must be
    // delivered only from generate().
    void advance_clock(uint32_t clocks);
    void process_timers();
    void deliver_irq();
    bool chip_offset(uint16_t port, uint32_t *offset) const;
    bool port_masked(uint32_t offset) const;

    struct pmd_ymfm_config config;
    std::unique_ptr<chip_interface> chip;
    std::vector<uint8_t> adpcm_a;
    std::vector<uint8_t> adpcm_b;
    // At most one missing-ROM warning per instance, as pmd_ymfm.h documents.
    bool rom_warned = false;

    void (*handler)(struct pmd *) = nullptr;

    uint32_t rate = 0;
    uint64_t clocks = 0;
    uint64_t busy_end = 0;
    uint32_t clocks_per_sample = 1;
    bool irq = false;
    struct {
        bool active;
        uint64_t expire;
    } timer[2] = {};

    // The board answers register 0Eh, not the chip, so the read is intercepted
    // before it reaches the chip.
    uint8_t address_latch = 0;

    // NEC OPNA ID port and mute control, which INT 60h and the probe touch.
    uint8_t opna_id = 0x00;
    uint8_t mute = 0x00;
    // The system VSync control port the driver brackets its FM interrupt
    // handler with. It is host-side state, not reachable through the chip.
    uint8_t vsync_port2 = 0x00;
};

pmd_ymfm::pmd_ymfm(const struct pmd_ymfm_config &cfg) : config(cfg) {
    switch (config.chip) {
    case PMD_YMFM_YM2203:
        chip.reset(new chip_model<ymfm::ym2203>(*this));
        break;
    case PMD_YMFM_YM2608:
        chip.reset(new chip_model<ymfm::ym2608>(*this));
        break;
    case PMD_YMFM_YMF288:
        chip.reset(new chip_model<ymfm::ymf288>(*this));
        break;
    }

    uint32_t input_clock = config.input_clock;
    if (input_clock == 0) {
        input_clock = config.chip == PMD_YMFM_YM2203 ? ym2203_clock
                                                     : ym2608_clock;
    }
    rate = chip->sample_rate(input_clock);
    // Rounded, not truncated: YMF288's sample rate does not divide its clock
    // evenly, and a truncated step would run the timer clocks 0.7% fast.
    clocks_per_sample = rate != 0 ? (input_clock + rate / 2) / rate : 1;
    if (clocks_per_sample == 0) clocks_per_sample = 1;

    adpcm_a.assign(adpcm_a_size, 0);
    adpcm_b.assign(adpcm_b_size, 0);
    chip->reset();
}

// ymfm hands the driver's timer register periods over as input clocks; they
// are one-shot, and the driver reloads them from its own handler.
void pmd_ymfm::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) {
    if (tnum >= 2) return;
    if (duration_in_clocks < 0) {
        timer[tnum].active = false;
        return;
    }
    timer[tnum].active = true;
    timer[tnum].expire = clocks + static_cast<uint32_t>(duration_in_clocks);
}

void pmd_ymfm::ymfm_set_busy_end(uint32_t clocks) {
    busy_end = this->clocks + clocks;
}

bool pmd_ymfm::ymfm_is_busy() { return clocks < busy_end; }

// The handler runs from generate(), never from here: YMFM reports IRQ changes
// from inside register writes, and re-entering the driver from one of its own
// port accesses would break the exclusion the host has to provide.
void pmd_ymfm::ymfm_update_irq(bool asserted) { irq = asserted; }

uint8_t pmd_ymfm::ymfm_external_read(ymfm::access_class type,
                                     uint32_t address) {
    if (type == ymfm::ACCESS_ADPCM_A) {
        return address < adpcm_a.size() ? adpcm_a[address] : 0;
    }
    if (type == ymfm::ACCESS_ADPCM_B) {
        return adpcm_b[address & (adpcm_b_size - 1)];
    }
    return 0;
}

void pmd_ymfm::ymfm_external_write(ymfm::access_class type, uint32_t address,
                                   uint8_t data) {
    if (type == ymfm::ACCESS_ADPCM_B) {
        adpcm_b[address & (adpcm_b_size - 1)] = data;
    }
}

void pmd_ymfm::advance_clock(uint32_t clocks) { this->clocks += clocks; }

void pmd_ymfm::process_timers() {
    for (uint32_t tnum = 0; tnum < 2; tnum++) {
        if (!timer[tnum].active || clocks < timer[tnum].expire) continue;
        timer[tnum].active = false;
        m_engine->engine_timer_expired(tnum);
    }
}

// Delivering after each generated sample keeps the timer work close to the
// expiry that caused it while staying outside every driver call.
void pmd_ymfm::deliver_irq() {
    void (*fn)(struct pmd *) = handler;
    if (!irq || fn == nullptr || config.pmd == nullptr) return;
    fn(config.pmd);
}

bool pmd_ymfm::chip_offset(uint16_t port, uint32_t *offset) const {
    if (port < config.base_port) return false;
    uint32_t delta = static_cast<uint32_t>(port - config.base_port);
    if (config.board == PMD_YMFM_BOARD_VA) {
        if (delta > 3) return false;
        *offset = delta;
        return true;
    }
    if (delta > 6 || (delta & 1) != 0) return false;
    *offset = delta >> 1;
    return true;
}

// The 86B decodes the chip's upper bank (base+4/6) only once init_86b turns
// the OPNA back on: that is what lets check_86b recognise the board while the
// bypassed interface reads as an open bus.
bool pmd_ymfm::port_masked(uint32_t offset) const {
    return config.board == PMD_YMFM_BOARD_PC98_86B && offset >= 2 &&
           (opna_id & 1) == 0;
}

// The driver brackets its FM interrupt handler with a read-modify-write of the
// system VSync port. The bits are not chip state, so the backend records them
// and gives them back.
static constexpr uint16_t vsync_port = 0x0002;

uint8_t pmd_ymfm::inb(uint16_t port) {
    advance_clock(clocks_per_sample);

    uint32_t offset;
    if (chip_offset(port, &offset)) {
        if (port_masked(offset)) return 0xff;
        // The board, not the chip, answers register 0Eh: bits 6-7 are the
        // DIP-selected interrupt level and bits 0-5 are the joystick inputs.
        // The backend has no joystick, so the inputs read as idle.
        if (offset == 1 && address_latch == 0x0e) {
            return static_cast<uint8_t>(config.irq_bits | 0x3f);
        }
        return chip->read(offset);
    }

    if (port == vsync_port) return vsync_port2;
    if (config.board != PMD_YMFM_BOARD_VA) {
        if (port == 0xa460) return opna_id;
        if (port == 0xa66e) return mute;
    }
    return 0xff;
}

void pmd_ymfm::outb(uint16_t port, uint8_t value) {
    advance_clock(clocks_per_sample);

    uint32_t offset;
    if (chip_offset(port, &offset)) {
        if (port_masked(offset)) return;
        if (offset == 0) address_latch = value;
        chip->write(offset, value);
        return;
    }

    if (port == vsync_port) {
        vsync_port2 = value;
        return;
    }
    if (config.board != PMD_YMFM_BOARD_VA) {
        if (port == 0xa460) opna_id = value;
        else if (port == 0xa66e) mute = value;
    }
    // 5Fh, 6Eh and the PIC ports are not chip state; a real-hardware host
    // models them.
}

void pmd_ymfm::generate(uint32_t samples, int32_t *out) {
    for (uint32_t i = 0; i < samples; i++) {
        int32_t frame[2] = {0, 0};
        chip->generate(frame);
        if (out != nullptr) {
            out[2 * i] = frame[0];
            out[2 * i + 1] = frame[1];
        }
        advance_clock(clocks_per_sample);
        process_timers();
        deliver_irq();
    }
}

extern "C" void pmd_ymfm_default_config(struct pmd_ymfm_config *config,
                                        struct pmd *pmd) {
    std::memset(config, 0, sizeof(*config));
    config->irq_bits = 0xc0; // int 5, the PC-98 default
#if va && board2
    config->chip = PMD_YMFM_YM2608;
    config->board = PMD_YMFM_BOARD_VA;
    config->base_port = 0x44;
#elif va
    config->chip = PMD_YMFM_YM2203;
    config->board = PMD_YMFM_BOARD_VA;
    config->base_port = 0x44;
#elif board2 && (PMD_YMFM_PCM || ppz)
    config->chip = PMD_YMFM_YM2608;
    config->board = PMD_YMFM_BOARD_PC98_86B;
    config->base_port = 0x188;
#else
    config->chip = PMD_YMFM_YM2608;
    config->board = PMD_YMFM_BOARD_PC98;
    config->base_port = 0x088;
#endif
    config->pmd = pmd;
}

extern "C" struct pmd_ymfm *
pmd_ymfm_create(const struct pmd_ymfm_config *config) {
    if (config == nullptr) return nullptr;
    switch (config->chip) {
    case PMD_YMFM_YM2203:
    case PMD_YMFM_YM2608:
    case PMD_YMFM_YMF288:
        break;
    default:
        return nullptr;
    }
    // Nothing may unwind through the C ABI: the chip and the sample memories
    // allocate during construction.
    try {
        return new (std::nothrow) pmd_ymfm(*config);
    } catch (...) {
        return nullptr;
    }
}

extern "C" void pmd_ymfm_destroy(struct pmd_ymfm *ymfm) { delete ymfm; }

extern "C" uint8_t pmd_ymfm_inb(struct pmd_ymfm *ymfm, uint16_t port) {
    return ymfm->inb(port);
}

extern "C" void pmd_ymfm_outb(struct pmd_ymfm *ymfm, uint16_t port,
                              uint8_t value) {
    ymfm->outb(port, value);
}

extern "C" void pmd_ymfm_generate(struct pmd_ymfm *ymfm, uint32_t samples,
                                  int32_t *out) {
    ymfm->generate(samples, out);
}

extern "C" uint32_t pmd_ymfm_sample_rate(const struct pmd_ymfm *ymfm) {
    return ymfm->rate;
}

extern "C" void pmd_ymfm_load_adpcm_a(struct pmd_ymfm *ymfm,
                                      const uint8_t *data, size_t size) {
    if (size > ymfm->adpcm_a.size()) size = ymfm->adpcm_a.size();
    std::memset(ymfm->adpcm_a.data(), 0, ymfm->adpcm_a.size());
    if (size != 0) std::memcpy(ymfm->adpcm_a.data(), data, size);
}

extern "C" void pmd_ymfm_load_adpcm_a_file(struct pmd_ymfm *ymfm,
                                           const char *path) {
    std::FILE *file = std::fopen(path, "rb");
    std::vector<uint8_t> image;
    if (file != nullptr) {
        uint8_t chunk[4096];
        size_t got;
        while ((got = std::fread(chunk, 1, sizeof(chunk), file)) != 0) {
            image.insert(image.end(), chunk, chunk + got);
        }
        std::fclose(file);
    }

    if (image.empty()) {
        pmd_ymfm_load_adpcm_a(ymfm, nullptr, 0);
        struct pmd *pmd = ymfm->config.pmd;
        if (!ymfm->rom_warned && pmd != nullptr && pmd->sys != nullptr) {
            ymfm->rom_warned = true;
            pmd->sys->print(rom_missing, pmd->user_data);
        }
        return;
    }
    pmd_ymfm_load_adpcm_a(ymfm, image.data(), image.size());
}

extern "C" uint8_t pmd_ymfm_adpcm_a_byte(const struct pmd_ymfm *ymfm,
                                         uint32_t address) {
    return address < ymfm->adpcm_a.size() ? ymfm->adpcm_a[address] : 0;
}

static uint8_t hw_inb(uint16_t port, void *user_data) {
    return static_cast<pmd_ymfm *>(user_data)->inb(port);
}

static void hw_outb(uint16_t port, uint8_t value, void *user_data) {
    static_cast<pmd_ymfm *>(user_data)->outb(port, value);
}

static void hw_install_fm_timer_handler(void (*handler)(struct pmd *),
                                        void *user_data) {
    static_cast<pmd_ymfm *>(user_data)->handler = handler;
}

static void hw_remove_fm_timer_handler(void *user_data) {
    pmd_ymfm *ymfm = static_cast<pmd_ymfm *>(user_data);
    ymfm->handler = nullptr;
    ymfm->irq = false;
}

// C++14 has no designated initializers, so the members are labeled in order.
extern "C" const struct pmd_hw pmd_hw_ymfm = {
    hw_inb, // inb
    hw_outb, // outb
    hw_install_fm_timer_handler, // install_fm_timer_handler
    hw_remove_fm_timer_handler, // remove_fm_timer_handler
    nullptr, // read_key_state
};
