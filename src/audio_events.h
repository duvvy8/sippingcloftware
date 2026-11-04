#pragma once
#include "common.h"
namespace clip {
// IMMNotificationClient callbacks only compare immutable IDs and set atomic flags.
// String formatting and all waiting/unregistration occur outside the callbacks.
class AudioDeviceEvents {
    std::vector<std::wstring> ids_;
    QStringList names_;
    bool output_ = false, input_ = false;
    std::atomic_int invalidated_ = -1;
    std::atomic_uint defaults_ = 0;
    std::atomic_bool properties_ = false;

  public:
    explicit AudioDeviceEvents(const QList<AudioSource> &sources) {
        for (const auto &source : sources)
            if (source.enabled && source.kind != "application") {
                ids_.push_back(source.id.toStdWString());
                names_ << source.name;
                input_ |= source.kind == "microphone";
                output_ |= source.kind == "system";
            }
    }
    void defaultChanged(bool input) noexcept {
        if ((input && input_) || (!input && output_))
            defaults_.fetch_or(input ? 2u : 1u);
    }
    void deviceState(const wchar_t *id, bool active) noexcept {
        if (!id || active)
            return;
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == id) {
                int empty = -1;
                invalidated_.compare_exchange_strong(empty, int(i));
                break;
            }
    }
    void propertiesChanged(const wchar_t *id) noexcept {
        if (id)
            for (const auto &selected : ids_)
                if (selected == id) {
                    properties_ = true;
                    break;
                }
    }
    QString error() const {
        int index = invalidated_.load();
        return index < 0 ? QString()
                         : names_.value(index) + ": selected audio device disconnected or became "
                                                 "unavailable. No replacement was selected.";
    }
    QString notice() const {
        unsigned changed = defaults_.load();
        QStringList messages;
        if (changed & 1)
            messages
                << "Windows default output changed; recording keeps the explicitly selected output device.";
        if (changed & 2)
            messages
                << "Windows default microphone changed; recording keeps the explicitly selected microphone.";
        if (properties_)
            messages << "Selected audio device properties changed; its endpoint selection remains fixed.";
        return messages.join(' ');
    }
};
} // namespace clip
