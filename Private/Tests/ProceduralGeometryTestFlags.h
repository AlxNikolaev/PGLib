// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Shared automation flags, inline so unity-merged test translation units do not redefine them.

inline constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;

inline constexpr EAutomationTestFlags SmokeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::HighPriority;

inline constexpr EAutomationTestFlags PerfTestFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter | EAutomationTestFlags::LowPriority;

#endif
