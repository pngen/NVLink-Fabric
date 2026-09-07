#pragma once
#include "nvlinkfabric/enums.hpp"
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Evidence: a typed provenance stamp for every observation and result.
// REAL / SYNTHETIC / UNSUPPORTED is always preserved; it is never inferred,
// silently upgraded, or lost in serialization.
//---------------------------------------------------------------------------
struct Evidence {
    EvidenceClass evidence_class{EvidenceClass::UNSUPPORTED};
    Provenance  provenance{Provenance::NONE};
    std::string source;   // producing subsystem (backend name, etc.)
    std::string note;     // human-readable qualification, e.g. why unsupported

    bool is_real() const noexcept { return evidence_class == EvidenceClass::REAL; }
    bool is_synthetic() const noexcept { return evidence_class == EvidenceClass::SYNTHETIC; }
    bool is_unsupported() const noexcept { return evidence_class == EvidenceClass::UNSUPPORTED; }

    std::string describe() const {
        std::string out = evidence_class_name(evidence_class);
        const std::string prov = provenance_name(provenance);
        if (!prov.empty() && prov != "NONE") {
            out += " ";
            out += prov;
        }
        if (!source.empty()) {
            out += " from=";
            out += source;
        }
        if (!note.empty()) {
            out += " (";
            out += note;
            out += ")";
        }
        return out;
    }

    bool operator==(const Evidence& o) const noexcept {
        return evidence_class == o.evidence_class && provenance == o.provenance &&
               source == o.source && note == o.note;
    }
    bool operator!=(const Evidence& o) const noexcept { return !(*this == o); }
};

}  // namespace nvlinkfabric
