#ifndef MEDIA_MEDIA_TYPES_HPP
#define MEDIA_MEDIA_TYPES_HPP

#include <functional>
#include <string>

enum class MediaComponent {
    Camera,
    Advertisement
};

enum class MediaState {
    Starting,
    Playing,
    Stopped,
    Restarting,
    Failed
};

struct MediaStatusEvent {
    MediaComponent component{MediaComponent::Camera};
    MediaState state{MediaState::Stopped};
    std::string currentFile;
    std::string reason;
};

using MediaStatusCallback = std::function<void(const MediaStatusEvent&)>;

#endif // MEDIA_MEDIA_TYPES_HPP
