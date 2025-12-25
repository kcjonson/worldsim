#pragma once

// NotificationManager - Simple toast notification system.
// Displays temporary messages that fade out after a short duration.
// Used for "Aha!" moments like recipe discoveries.

#include <chrono>
#include <deque>
#include <string>

namespace world_sim {

/// A single notification message
struct Notification {
	std::string message;
	std::chrono::steady_clock::time_point createdAt;
	float duration = 4.0F; // Seconds to display

	/// Get age in seconds
	[[nodiscard]] float age() const {
		auto now = std::chrono::steady_clock::now();
		return std::chrono::duration<float>(now - createdAt).count();
	}

	/// Check if notification has expired
	[[nodiscard]] bool isExpired() const { return age() >= duration; }

	/// Get opacity (fades out in last second)
	[[nodiscard]] float opacity() const {
		float remaining = duration - age();
		if (remaining <= 0.0F) {
			return 0.0F;
		}
		if (remaining < 1.0F) {
			return remaining; // Fade out
		}
		return 1.0F;
	}
};

/// Manages a queue of toast notifications
class NotificationManager {
  public:
	/// Maximum notifications to display at once
	static constexpr size_t kMaxVisible = 3;

	/// Add a new notification
	/// @param message The notification text to display
	/// @param duration How long to show the notification in seconds (default: 4s)
	void push(const std::string& message, float duration = 4.0F) {
		m_notifications.push_back({message, std::chrono::steady_clock::now(), duration});
	}

	/// Remove expired notifications
	void update() {
		while (!m_notifications.empty() && m_notifications.front().isExpired()) {
			m_notifications.pop_front();
		}
	}

	/// Get active notifications (oldest first - FIFO order, limited to kMaxVisible)
	[[nodiscard]] const std::deque<Notification>& notifications() const { return m_notifications; }

	/// Check if there are any active notifications
	[[nodiscard]] bool hasNotifications() const { return !m_notifications.empty(); }

  private:
	std::deque<Notification> m_notifications;
};

} // namespace world_sim
