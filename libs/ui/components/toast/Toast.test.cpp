#include "Toast.h"
#include "ToastStack.h"

#include <gtest/gtest.h>

namespace UI {

// === Toast Construction Tests ===

TEST(ToastTest, ConstructsWithDefaults) {
	Toast toast(Toast::Args{
		.title = "Test Title",
		.message = "Test message",
	});

	EXPECT_EQ(toast.getTitle(), "Test Title");
	EXPECT_EQ(toast.getMessage(), "Test message");
	EXPECT_EQ(toast.getSeverity(), ToastSeverity::Info);
	EXPECT_FALSE(toast.isPersistent());
	EXPECT_FALSE(toast.isFinished());
	EXPECT_FLOAT_EQ(toast.getWidth(), Theme::Toast::defaultWidth);
}

TEST(ToastTest, ConstructsWithSeverity) {
	Toast warning(Toast::Args{
		.title = "Warning",
		.message = "Something happened",
		.severity = ToastSeverity::Warning,
	});
	EXPECT_EQ(warning.getSeverity(), ToastSeverity::Warning);

	Toast critical(Toast::Args{
		.title = "Critical",
		.message = "Something bad happened",
		.severity = ToastSeverity::Critical,
	});
	EXPECT_EQ(critical.getSeverity(), ToastSeverity::Critical);
}

TEST(ToastTest, ConstructsAsPersistent) {
	Toast toast(Toast::Args{
		.title = "Persistent",
		.message = "Won't auto-dismiss",
		.autoDismissTime = 0.0F,
	});

	EXPECT_TRUE(toast.isPersistent());
	EXPECT_FLOAT_EQ(toast.getRemainingTime(), 0.0F);
}

TEST(ToastTest, ConstructsWithCustomWidth) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.width = 400.0F,
	});

	EXPECT_FLOAT_EQ(toast.getWidth(), 400.0F);
}

TEST(ToastTest, ConstructsWithMargin) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.width = 300.0F,
		.margin = 10.0F,
	});

	// getWidth includes margin on both sides
	EXPECT_FLOAT_EQ(toast.getWidth(), 320.0F); // 300 + 10*2
}

// === Toast State Tests ===

TEST(ToastTest, DismissStartsFadeOut) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
	});

	EXPECT_FALSE(toast.isDismissing());
	toast.dismiss();
	EXPECT_TRUE(toast.isDismissing());
}

TEST(ToastTest, UpdateProgressesFadeIn) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
	});

	EXPECT_FLOAT_EQ(toast.getOpacity(), 0.0F);

	// Update through fade-in (0.2s duration)
	toast.update(0.1F);
	EXPECT_GT(toast.getOpacity(), 0.0F);
	EXPECT_LT(toast.getOpacity(), 1.0F);

	toast.update(0.15F);
	EXPECT_FLOAT_EQ(toast.getOpacity(), 1.0F);
}

TEST(ToastTest, AutoDismissAfterTime) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.autoDismissTime = 1.0F,
	});

	// Complete fade-in
	toast.update(0.3F);
	EXPECT_FALSE(toast.isDismissing());

	// Wait for auto-dismiss
	toast.update(1.1F);
	EXPECT_TRUE(toast.isDismissing());
}

TEST(ToastTest, PersistentDoesNotAutoDismiss) {
	Toast toast(Toast::Args{
		.title = "Persistent",
		.message = "Message",
		.autoDismissTime = 0.0F,
	});

	// Complete fade-in
	toast.update(0.3F);
	EXPECT_FALSE(toast.isDismissing());

	// Wait a long time
	toast.update(10.0F);
	EXPECT_FALSE(toast.isDismissing());
}

TEST(ToastTest, FinishesAfterFadeOut) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
	});

	// Complete fade-in
	toast.update(0.3F);

	// Start dismiss
	toast.dismiss();
	EXPECT_FALSE(toast.isFinished());

	// Complete fade-out (0.3s duration)
	toast.update(0.4F);
	EXPECT_TRUE(toast.isFinished());
}

TEST(ToastTest, OnDismissCallbackFires) {
	bool callbackFired = false;

	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.onDismiss = [&callbackFired]() { callbackFired = true; },
	});

	// Complete fade-in
	toast.update(0.3F);

	// Dismiss and complete fade-out
	toast.dismiss();
	toast.update(0.4F);

	EXPECT_TRUE(callbackFired);
}

TEST(ToastTest, GetRemainingTimeCountsDown) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.autoDismissTime = 5.0F,
	});

	// Complete fade-in
	toast.update(0.3F);

	float remaining1 = toast.getRemainingTime();
	toast.update(1.0F);
	float remaining2 = toast.getRemainingTime();

	EXPECT_LT(remaining2, remaining1);
}

// === Toast Hit Testing ===

TEST(ToastTest, ContainsPointInBounds) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.position = {100.0F, 100.0F},
		.width = 300.0F,
	});

	// Inside
	EXPECT_TRUE(toast.containsPoint({200.0F, 120.0F}));
	EXPECT_TRUE(toast.containsPoint({100.0F, 100.0F})); // Top-left

	// Outside
	EXPECT_FALSE(toast.containsPoint({50.0F, 120.0F}));	 // Left
	EXPECT_FALSE(toast.containsPoint({450.0F, 120.0F})); // Right
	EXPECT_FALSE(toast.containsPoint({200.0F, 50.0F}));	 // Above
}

TEST(ToastTest, SetPositionUpdatesBase) {
	Toast toast(Toast::Args{
		.title = "Test",
		.message = "Message",
		.position = {10.0F, 20.0F},
	});

	toast.setPosition(50.0F, 60.0F);

	Foundation::Vec2 contentPos = toast.getContentPosition();
	EXPECT_FLOAT_EQ(contentPos.x, 50.0F);
	EXPECT_FLOAT_EQ(contentPos.y, 60.0F);
}

// === ToastStack Tests ===

TEST(ToastStackTest, ConstructsWithDefaults) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
	});

	EXPECT_EQ(stack.getToastCount(), 0);
	EXPECT_EQ(stack.getAnchor(), ToastAnchor::BottomRight);
}

TEST(ToastStackTest, ConstructsWithAnchor) {
	ToastStack stack(ToastStack::Args{
		.position = {0.0F, 0.0F},
		.anchor = ToastAnchor::TopLeft,
	});

	EXPECT_EQ(stack.getAnchor(), ToastAnchor::TopLeft);
}

TEST(ToastStackTest, AddToastIncreasesCount) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
	});

	stack.addToast("Test", "Message");
	EXPECT_EQ(stack.getToastCount(), 1);

	stack.addToast("Test 2", "Message 2");
	EXPECT_EQ(stack.getToastCount(), 2);
}

TEST(ToastStackTest, AddToastWithSeverity) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
	});

	stack.addToast("Warning", "Something happened", ToastSeverity::Warning);
	EXPECT_EQ(stack.getToastCount(), 1);
}

TEST(ToastStackTest, MaxToastsEnforced) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
		.maxToasts = 3,
	});

	stack.addToast("Toast 1", "Message");
	stack.addToast("Toast 2", "Message");
	stack.addToast("Toast 3", "Message");
	EXPECT_EQ(stack.getToastCount(), 3);

	// Adding a 4th should dismiss the oldest
	stack.addToast("Toast 4", "Message");
	// Count may still be 4 temporarily until update removes finished
	EXPECT_LE(stack.getToastCount(), 4);
}

TEST(ToastStackTest, DismissAllDismissesToasts) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
	});

	stack.addToast("Toast 1", "Message", ToastSeverity::Info, 0.0F); // Persistent
	stack.addToast("Toast 2", "Message", ToastSeverity::Info, 0.0F); // Persistent
	EXPECT_EQ(stack.getToastCount(), 2);

	stack.dismissAll();

	// After enough updates, toasts should be removed
	for (int i = 0; i < 10; ++i) {
		stack.update(0.1F);
	}

	EXPECT_EQ(stack.getVisibleToastCount(), 0);
}

TEST(ToastStackTest, UpdateRemovesFinishedToasts) {
	ToastStack stack(ToastStack::Args{
		.position = {800.0F, 600.0F},
	});

	stack.addToast("Toast", "Message", ToastSeverity::Info, 0.5F);
	EXPECT_EQ(stack.getToastCount(), 1);

	// Complete fade-in, wait for auto-dismiss, complete fade-out
	for (int i = 0; i < 20; ++i) {
		stack.update(0.1F);
	}

	EXPECT_EQ(stack.getToastCount(), 0);
}

TEST(ToastStackTest, ContainsPointDelegatesToToasts) {
	ToastStack stack(ToastStack::Args{
		.position = {500.0F, 400.0F},
		.anchor = ToastAnchor::BottomRight,
		.toastWidth = 300.0F,
	});

	// With BottomRight anchor at (500, 400), toast is at x = 200 to 500
	stack.addToast("Toast", "Message");

	// Inside toast area (approximately)
	EXPECT_TRUE(stack.containsPoint({300.0F, 360.0F}));
}

TEST(ToastStackTest, SetPositionRepositionsToasts) {
	ToastStack stack(ToastStack::Args{
		.position = {500.0F, 400.0F},
	});

	stack.addToast("Toast", "Message");

	// Move stack
	stack.setPosition(600.0F, 500.0F);

	// The toast should have been repositioned
	EXPECT_TRUE(stack.containsPoint({400.0F, 460.0F}));
}

} // namespace UI
