// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Shared automation flags for ProceduralGeometry tests. Defined once here (inline)
// so the constant is not redefined across test files that share a translation unit.

// Default: run in editor and game contexts, product-level test with medium priority.
inline constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;

// SmokeTestFlags: quick smoke tests.
inline constexpr EAutomationTestFlags SmokeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::HighPriority;

// PerfTestFlags: performance tests.
inline constexpr EAutomationTestFlags PerfTestFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::PerfFilter | EAutomationTestFlags::LowPriority;

#endif
