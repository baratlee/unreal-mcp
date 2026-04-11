// UnrealMCP Private PCH - suppress engine header warnings for UE 5.7 compatibility

#pragma once

// UE 5.7: StringConv.h triggers C4459 (local declaration hides global declaration)
#pragma warning(disable: 4459)

#include "CoreMinimal.h"
