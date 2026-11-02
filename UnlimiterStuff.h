#include "PresetCarSlot.h"
#include <string.h>

#define CarTypeInfoArray 0x734588
#define CarDBInstance 0x758C28
#define StoreCapacity 127
#define StoreElementSize 0x348

// The global instance is the FEPlayerCarDB member (+0x364) of the active
// UserProfile (0x7588C4); the savegame pack/loader copy that member raw, so
// the save/load path only ever sees the in-place store below.
#define SaveGameProfile 0x7588C4    // active UserProfile; +0x364 == CarDBInstance
#define InPlaceStoreBase 0x75AC60   // CarDBInstance + 0x2038 (in-place preset store)
#define InPlaceCountPtr 0x75EE00    // CarDBInstance + 0x61D8 (in-place element count)
#define StockStoreCount 20          // preset elements the stock savegame format holds

int CarArraySize, CarCount, i;
bool SortCarsByID, DisappearingWheelsFix;

// --- Extended preset-car store ---------------------------------------------
//
// The global FEPlayerCarDB (0x758C28) keeps its preset-car store (20 elements
// at +0x2038, count at +0x61D8) in a region that can neither grow nor move,
// so the store is relocated into ExtCarStore below and every access site
// branches on the instance pointer: the global instance is served from
// ExtCarStore/ExtCarCount, every other (profile member) instance keeps the
// original in-place 20-car behavior.

BYTE ExtCarStore[StoreCapacity][StoreElementSize];  // preset elements of the global instance
unsigned int ExtCarCount;                           // element count of the global instance
unsigned int ExtStoreBase = (unsigned int)ExtCarStore;
unsigned int ExtStoreNameField = (unsigned int)ExtCarStore + 0x344; // first element's +0x344 field
unsigned int ExtStoreHashField = (unsigned int)ExtCarStore + 0x8;   // first element's +8 hash field
unsigned int ExtStoreEnd = (unsigned int)ExtCarStore + StoreCapacity * StoreElementSize;
unsigned int ExtLabelBase = (unsigned int)ExtCarStore + 0x344;                          // first element's +0x344 label field
unsigned int ExtLabelBound = (unsigned int)ExtCarStore + StoreCapacity * StoreElementSize + 0x344; // one past the last label field
int TotalRacers;                                    // total racer count (HashesCount - 8)

// Fixed call targets for the element build, invoked through memory operands
// so the caves stay position independent
unsigned int FormatLabel = 0x4F42F0;        // game: builds the frontend label of a CarTypeInfo
unsigned int BuildPresetElement = 0x4ACB90; // game: builds one preset-car element

bool IsRacer(BYTE CarTypeID)
{
	if (CarTypeID >= CarCount) return 0;
	return *(BYTE*)((*(DWORD*)CarTypeInfoArray) + CarTypeID * 0xC90 + 0xC54) == 0;
}

unsigned int GetCarTypeNameHash(BYTE CarTypeID)
{
	if (CarTypeID >= CarCount) return 0;
	return *(DWORD*)((*(DWORD*)CarTypeInfoArray) + CarTypeID * 0xC90 + 0xD0);
}

// --- DefaultBasePaint restore ----------------------------------------------
//
// The unique pseudo catalog keys (0x80000000|index) written to CarTypeInfo+0xC5C
// stop addon records from shadowing stock ones, but the same field doubles as
// the BASE_PAINT lookup key, so the addon cars' stock paint lookup would miss.
// RealBasePaint keeps each original key and the four caves below translate it
// back in the paint lookups; stock cars are untouched.

unsigned int RealBasePaint[StoreCapacity]; // original CarTypeInfo+0xC5C per addon car index

unsigned int __fastcall ResolveBasePaint(unsigned int key, unsigned int info)
{
	unsigned int base = *(unsigned int*)CarTypeInfoArray;
	if (info < base || (info - base) % 0xC90 != 0)
		return key;
	unsigned int idx = (info - base) / 0xC90;
	if (idx >= (unsigned int)CarCount)
		return key;
	unsigned int real = RealBasePaint[idx];
	return real ? real : key;
}

// 0x4ACC5B: mov ecx, [edi+0xC5C] in FECarConfig::SetDefaults, re-enters at 0x4ACC61
void __declspec(naked) BasePaintCave1()
{
	_asm
	{
		mov ecx, dword ptr [edi+0xC5C]
		mov edx, edi
		call ResolveBasePaint
		mov ecx, eax
		push 0x4ACC61
		retn
	}
}

// 0x4BCAAC: mov edx, [esi+ecx+0xC5C] in MakeRideStock (esi = type*0xC90,
// ecx = CarTypeInfoArray), re-enters at 0x4BCAB3
void __declspec(naked) BasePaintCave2()
{
	_asm
	{
		mov edx, dword ptr [esi+ecx+0xC5C]
		push eax
		mov eax, edx
		lea edx, dword ptr [esi+ecx]
		mov ecx, eax
		call ResolveBasePaint
		mov edx, eax
		pop eax
		push 0x4BCAB3
		retn
	}
}

// 0x4C2DC8: mov ecx, [eax+0xC5C] in sub_4C2D20 (eax = CarTypeInfo pointer),
// re-enters at 0x4C2DCE
void __declspec(naked) BasePaintCave3()
{
	_asm
	{
		mov ecx, dword ptr [eax+0xC5C]
		mov edx, eax
		call ResolveBasePaint
		mov ecx, eax
		push 0x4C2DCE
		retn
	}
}

// 0x4C4294: mov ecx, [esi+0xC5C] in sub_4C4230 (esi = CarTypeInfo pointer),
// re-enters at 0x4C429A
void __declspec(naked) BasePaintCave4()
{
	_asm
	{
		mov ecx, dword ptr [esi+0xC5C]
		mov edx, esi
		call ResolveBasePaint
		mov ecx, eax
		push 0x4C429A
		retn
	}
}

// --- ThumbnailScroller null-current guard -----------------------------------
//
// The frontend ships a fixed pool of "car_thumb" slot objects; with more
// listed cars than slots the scroller's current item stays null and
// ThumbnailScroller::Update dereferences it (crash at 0x4F8A12). The cave
// falls back to the first node instead; cars without a slot keep working
// everywhere else, they just get no thumbnail highlight.

// 0x4F8A0C: push ebx / push ebp / mov ebp, [edi+0Ch] in ThumbnailScroller::Update.
// If the current item is null, fall back to the first node and store it back
// so Update's later re-reads see it too; re-enters at 0x4F8A11 (push esi).
void __declspec(naked) ScrollerCurrentFixCave()
{
	_asm
	{
		push ebx
		push ebp
		mov ebp, dword ptr [edi+0Ch]
		test ebp, ebp
		jnz ok
		mov ebp, dword ptr [edi+4]
		mov dword ptr [edi+0Ch], ebp
ok:
		push 0x4F8A11
		retn
	}
}

// --- element count reads/writes (11 sites, 2 handled by other caves) -------

// 0x4AB57A: mov ebx, [eax+0x61D8], re-enters at 0x4AB580
void __declspec(naked) CountRead1CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old1
		mov ebx, dword ptr [ExtCarCount]
		jmp out1
	old1:
		mov ebx, dword ptr [eax+0x61D8]
	out1:
		push 0x4AB580
		retn
	}
}

// 0x4ABE80: mov [eax+0x61D8], esi (store reset), re-enters at 0x4ABE86
void __declspec(naked) CountWrite1CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old2
		mov dword ptr [ExtCarCount], esi
		jmp out2
	old2:
		mov dword ptr [eax+0x61D8], esi
	out2:
		push 0x4ABE86
		retn
	}
}

// 0x4ABEC4: mov ecx, [esi+0x61D8], re-enters at 0x4ABECA
void __declspec(naked) CountRead2CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old3
		mov ecx, dword ptr [ExtCarCount]
		jmp out3
	old3:
		mov ecx, dword ptr [esi+0x61D8]
	out3:
		push 0x4ABECA
		retn
	}
}

// 0x4ABEDB: mov ebx, [esi+0x61D8], re-enters at 0x4ABEE1
void __declspec(naked) CountRead3CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old4
		mov ebx, dword ptr [ExtCarCount]
		jmp out4
	old4:
		mov ebx, dword ptr [esi+0x61D8]
	out4:
		push 0x4ABEE1
		retn
	}
}

// 0x4AC071: mov edx, [esi+0x61D8], re-enters at 0x4AC077
void __declspec(naked) CountRead4CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old5
		mov edx, dword ptr [ExtCarCount]
		jmp out5
	old5:
		mov edx, dword ptr [esi+0x61D8]
	out5:
		push 0x4AC077
		retn
	}
}

// 0x4AC0B0: mov ecx, [esi+0x61D8], re-enters at 0x4AC0B6
void __declspec(naked) CountRead5CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old6
		mov ecx, dword ptr [ExtCarCount]
		jmp out6
	old6:
		mov ecx, dword ptr [esi+0x61D8]
	out6:
		push 0x4AC0B6
		retn
	}
}

// 0x4E0DB1: mov ecx, [eax+0x61D8], re-enters at 0x4E0DB7
void __declspec(naked) CountRead6CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old7
		mov ecx, dword ptr [ExtCarCount]
		jmp out7
	old7:
		mov ecx, dword ptr [eax+0x61D8]
	out7:
		push 0x4E0DB7
		retn
	}
}

// 0x4E0F0A: mov edx, [ecx+0x61D8], re-enters at 0x4E0F10
void __declspec(naked) CountRead7CodeCave()
{
	_asm
	{
		cmp ecx, CarDBInstance
		jne old8
		mov edx, dword ptr [ExtCarCount]
		jmp out8
	old8:
		mov edx, dword ptr [ecx+0x61D8]
	out8:
		push 0x4E0F10
		retn
	}
}

// --- store base accesses (11 sites, 1 handled by the build cave) -----------

// 0x4AB588: lea edi, [eax+0x2038], re-enters at 0x4AB58E
void __declspec(naked) StoreBase1CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old9
		mov edi, dword ptr [ExtStoreBase]
		jmp out9
	old9:
		lea edi, dword ptr [eax+0x2038]
	out9:
		push 0x4AB58E
		retn
	}
}

// 0x4ABE3F: lea edx, [eax+0x2038], re-enters at 0x4ABE45
void __declspec(naked) StoreBase2CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old10
		mov edx, dword ptr [ExtStoreBase]
		jmp out10
	old10:
		lea edx, dword ptr [eax+0x2038]
	out10:
		push 0x4ABE45
		retn
	}
}

// 0x4ABED0: lea ecx, [esi+0x237C], re-enters at 0x4ABED6
void __declspec(naked) StoreBase3CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old11
		mov ecx, dword ptr [ExtStoreNameField]
		jmp out11
	old11:
		lea ecx, dword ptr [esi+0x237C]
	out11:
		push 0x4ABED6
		retn
	}
}

// 0x4ABF18: lea ecx, [esi+0x237C], re-enters at 0x4ABF1E
void __declspec(naked) StoreBase4CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old12
		mov ecx, dword ptr [ExtStoreNameField]
		jmp out12
	old12:
		lea ecx, dword ptr [esi+0x237C]
	out12:
		push 0x4ABF1E
		retn
	}
}

// 0x4AC07D: lea ecx, [esi+0x237C], re-enters at 0x4AC083
void __declspec(naked) StoreBase5CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old13
		mov ecx, dword ptr [ExtStoreNameField]
		jmp out13
	old13:
		lea ecx, dword ptr [esi+0x237C]
	out13:
		push 0x4AC083
		retn
	}
}

// 0x4AC0BC: lea edx, [esi+0x237C], re-enters at 0x4AC0C2
void __declspec(naked) StoreBase6CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old14
		mov edx, dword ptr [ExtStoreNameField]
		jmp out14
	old14:
		lea edx, dword ptr [esi+0x237C]
	out14:
		push 0x4AC0C2
		retn
	}
}

// 0x4AC0FE: lea ecx, [esi+0x237C], re-enters at 0x4AC104
void __declspec(naked) StoreBase7CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old15
		mov ecx, dword ptr [ExtStoreNameField]
		jmp out15
	old15:
		lea ecx, dword ptr [esi+0x237C]
	out15:
		push 0x4AC104
		retn
	}
}

// 0x4ABFD6: add esi, 0x2040 (first element's +8 hash field), re-enters at 0x4ABFDC
void __declspec(naked) StoreBase8CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old21
		mov esi, dword ptr [ExtStoreHashField]
		jmp out21
	old21:
		add esi, 0x2040
	out21:
		push 0x4ABFDC
		retn
	}
}

// 0x4E0DCC: add eax, 0x2038, re-enters at 0x4E0DD1
void __declspec(naked) StoreBase9CodeCave()
{
	_asm
	{
		cmp eax, CarDBInstance
		jne old22
		mov eax, dword ptr [ExtStoreBase]
		jmp out22
	old22:
		add eax, 0x2038
	out22:
		push 0x4E0DD1
		retn
	}
}

// 0x4AC00B: cmp edi, 0x14 / jl 0x4ABFE0 - loop end of sub_4ABFC0's element
// search. Its not-found path dereferences a zero CarTypeInfo pointer and only
// survives because the stock 20 elements always resolve, so for the global
// instance the loop must stop at ExtCarCount instead of a static limit.
// (arg1/this sits at [esp+0x14]: four registers pushed in the prologue.)
void __declspec(naked) FindLoopBoundCodeCave()
{
	_asm
	{
		cmp dword ptr [esp+0x14], CarDBInstance
		jne member
		cmp edi, dword ptr [ExtCarCount]
		jl loophead
		jmp notfound
	member:
		cmp edi, 2
		jge memberbound
	memberbound:
		mov eax, dword ptr [esp+0x14]
		cmp edi, dword ptr [eax+0x61D8]
		jl loophead
		jmp notfound
	loophead:
		push 0x4ABFE0
		retn
	notfound:
		push 0x4AC010
		retn
	}
}

// 0x4AC52A: mov ecx, [eax+0x344] - DefaultCustomizableCars tail. sub_4ABFC0
// returns a null element when the requested car is not in the store (possible
// now that the store can contain addon cars) - and the tail dereferences it
// TWICE (0x4AC52A and 0x4AC534). The null path replicates the whole tail with
// zeroed values instead of re-entering it.
void __declspec(naked) DefaultCarTailCodeCave()
{
	_asm
	{
		test eax, eax
		jz nullcar
		mov ecx, dword ptr [eax+0x344]
		mov edx, dword ptr [eax+0x344]
		push 0x4AC530
		retn
	nullcar:
		xor ecx, ecx
		xor edx, edx
		pop edi
		mov dword ptr [ebp], ecx
		pop esi
		mov dword ptr [ebp+4], edx
		pop ebp
		pop ebx
		ret 8
	}
}

// --- element address computations (base + idx*0x348, indexed forms) --------

// 0x4ABEF4: lea eax, [eax+esi+0x2038], eax = idx*0x348, esi = this, re-enters at 0x4ABEFB
void __declspec(naked) StoreElem1CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old16
		add eax, dword ptr [ExtStoreBase]
		jmp out16
	old16:
		lea eax, dword ptr [eax+esi+0x2038]
	out16:
		push 0x4ABEFB
		retn
	}
}

// 0x4AC02D: lea eax, [edi+edx+0x2038], edi = idx*0x348, edx = this, re-enters at 0x4AC034
void __declspec(naked) StoreElem2CodeCave()
{
	_asm
	{
		cmp edx, CarDBInstance
		jne old17
		mov eax, edi
		add eax, dword ptr [ExtStoreBase]
		jmp out17
	old17:
		lea eax, dword ptr [edi+edx+0x2038]
	out17:
		push 0x4AC034
		retn
	}
}

// 0x4AC09D: lea eax, [eax+esi+0x2038], eax = idx*0x348, esi = this, re-enters at 0x4AC0A4
void __declspec(naked) StoreElem3CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old18
		add eax, dword ptr [ExtStoreBase]
		jmp out18
	old18:
		lea eax, dword ptr [eax+esi+0x2038]
	out18:
		push 0x4AC0A4
		retn
	}
}

// 0x4AC0DA: lea eax, [eax+esi+0x2038], eax = idx*0x348, esi = this, re-enters at 0x4AC0E1
void __declspec(naked) StoreElem4CodeCave()
{
	_asm
	{
		cmp esi, CarDBInstance
		jne old19
		add eax, dword ptr [ExtStoreBase]
		jmp out19
	old19:
		lea eax, dword ptr [eax+esi+0x2038]
	out19:
		push 0x4AC0E1
		retn
	}
}

// --- FEPlayerCarDB::DefaultCustomizableCars --------------------------------

// 0x4AC4B6: mov [ebp+0x61D8], edi (count reset on entry), re-enters at 0x4AC4BC
void __declspec(naked) CountResetCodeCave()
{
	_asm
	{
		cmp ebp, CarDBInstance
		jne old20
		mov dword ptr [ExtCarCount], edi
		jmp out20
	old20:
		mov dword ptr [ebp+0x61D8], edi
	out20:
		push 0x4AC4BC
		retn
	}
}

// 0x4AC4DD-0x4AC50F: element count read, 20-car limit, element build and
// count increment. Replaces the whole block and re-enters at 0x4AC510 (the
// loop tail). The global instance builds every racer into ExtCarStore; the
// member instances keep the original in-place 20-car behavior.
void __declspec(naked) DefaultCustomizableCarsCodeCave()
{
	_asm
	{
		cmp ebp, CarDBInstance
		jne member
		mov eax, dword ptr [ExtCarCount]
		cmp eax, dword ptr [TotalRacers]
		jge skipadd
		mov ebx, eax
		imul ebx, ebx, 0x348
		add ebx, dword ptr [ExtStoreBase]
		push ecx
		push 0x6C6460
		call dword ptr [FormatLabel]
		add esp, 8
		mov ecx, esi
		call dword ptr [BuildPresetElement]
		inc dword ptr [ExtCarCount]
		jmp skipadd
	member:
		mov eax, dword ptr [ebp+0x61D8]
		cmp eax, 0x14
		jge skipadd
		imul eax, eax, 0x348
		push ecx
		push 0x6C6460
		lea ebx, dword ptr [eax+ebp+0x2038]
		call dword ptr [FormatLabel]
		add esp, 8
		mov ecx, esi
		call dword ptr [BuildPresetElement]
		inc dword ptr [ebp+0x61D8]
	skipadd:
		push 0x4AC510
		retn
	}
}

// 0x4AC7A1-0x4AC7A7: mov edx, [ebp] / test edx, edx / je 0x4AC7D0 - the
// parts-list head read of FillWithRide's copy loop, replicated exactly.
void __declspec(naked) PartsHeadReadCodeCave()
{
	_asm
	{
		mov edx, dword ptr [ebp]
		test edx, edx
		je zerobranch
		push 0x4AC7A8
		retn
	zerobranch:
		push 0x4AC7D0
		retn
	}
}

// --- Savegame preset-ride bridge -------------------------------------------
//
// The save/load path never goes through the redirected store: the savegame pack
// copies the active profile's embedded FEPlayerCarDB raw (in-place store
// included) and the loaders write savegame rides straight back into it, so
// garage customizations (which land in ExtCarStore) never reach a savegame and
// loaded rides never become visible. Three bridges close both directions:
// pack 0x4AB165 mirrors ExtCarStore over the in-place store before the pack,
// unpack 0x4AB21D and sync 0x4CC7AD merge the in-place store back into
// ExtCarStore after a load. The merge never shrinks ExtCarCount, so the addon
// elements built at boot (indexes >= 20) survive a load.

// Copies the first min([InPlaceCountPtr], StoreCapacity) preset elements from
// the in-place store into ExtCarStore, raising ExtCarCount to cover them.
// All registers preserved.
void __declspec(naked) MergeInPlaceToExtStore()
{
	_asm
	{
		pushad
		mov eax, dword ptr ds:[InPlaceCountPtr]   // loaded element count
		test eax, eax
		jz mergeDone
		cmp eax, StoreCapacity
		jbe mergeCountOk
		mov eax, StoreCapacity
	mergeCountOk:
		cmp eax, dword ptr [ExtCarCount]
		jle mergeHaveBase                      // never shrink the count
		mov dword ptr [ExtCarCount], eax
	mergeHaveBase:
		mov edx, eax
		imul edx, edx, StoreElementSize        // byte count
		mov ecx, edx
		shr ecx, 2
		mov esi, InPlaceStoreBase
		mov edi, dword ptr [ExtStoreBase]
		rep movsd
	mergeDone:
		popad
		ret
	}
}

// 0x4AB165: sub_4AB120's preset store copy setup (ebp = source profile).
// Mirrors ExtCarStore (first 20 elements + count) over the in-place store so
// the raw blob copy packs current rides; only fires for the active profile.
void __declspec(naked) SavePackBridgeCodeCave()
{
	_asm
	{
		cmp ebp, SaveGameProfile
		jne packNoBridge
		pushad
		mov eax, dword ptr [ExtCarCount]
		cmp eax, StockStoreCount
		jbe packCountOk
		mov eax, StockStoreCount
	packCountOk:
		mov dword ptr ds:[InPlaceCountPtr], eax
		mov edx, eax
		imul edx, edx, StoreElementSize
		mov ecx, edx
		shr ecx, 2
		mov esi, dword ptr [ExtStoreBase]
		mov edi, InPlaceStoreBase
		rep movsd
		popad
	packNoBridge:
		mov ecx, 0x18B4
		lea esi, dword ptr [ebp+0x364]
		push 0x4AB170
		retn
	}
}

// 0x4AB21D: sub_4AB200's preset store copy (savegame blob -> profile's
// embedded FEPlayerCarDB). Unpacking into the active profile lands savegame
// rides in the in-place store, so merge them into ExtCarStore afterwards.
void __declspec(naked) LoadUnpackBridgeCodeCave()
{
	_asm
	{
		mov ecx, 0x18B4
		lea edi, dword ptr [edx+0x364]
		rep movsd
		cmp edx, SaveGameProfile
		jne unpackNoBridge
		call MergeInPlaceToExtStore
	unpackNoBridge:
		push 0x4AB22A
		retn
	}
}

// 0x4CC7AD: call sub_4CC7C0 - the career load sync's career-manager copy;
// edi holds the target stable base (callee-saved). For stable 0 (the global
// instance) the sync landed the loaded rides in the in-place store, so merge
// them into ExtCarStore. Hooking the 5-byte call avoids stomping the
// neighboring function's epilogue at 0x4CC7B5.
unsigned int SyncCareerManager = 0x4CC7C0; // game: copies the profile's FECareerManager member

void __declspec(naked) LoadSyncBridgeCodeCave()
{
	_asm
	{
		call dword ptr [SyncCareerManager]
		cmp edi, CarDBInstance
		jne syncNoBridge
		call MergeInPlaceToExtStore
	syncNoBridge:
		push 0x4CC7B2
		retn
	}
}

// --- Savegame trailer (addon preset elements) -------------------------------
//
// The .ugd savegame blob holds exactly 20 preset elements (the stock car
// types) and its trailing adler checksum covers the blob only, so the addon
// elements (ExtCarStore[20..count)) ride along in a trailer appended after it:
// [dword 0x31464C55 ("ULF1")][dword count][elements * 0x348]. Nothing is
// appended while ExtCarCount <= 20, keeping such saves byte-identical to
// stock, and the stock reader never touches anything past the checksum. On
// load the elements are placed back by their +8 car-type hash.

#define SaveTrailerMagic 0x31464C55 // "ULF1"
#define SaveTrailerHeaderSize 8

BYTE ExtTrailer[SaveTrailerHeaderSize + (StoreCapacity - StockStoreCount) * StoreElementSize];

// game CRT wrappers / import thunks, called through memory operands so the
// caves stay position independent
unsigned int GameFread = 0x67163D;  // game: fread wrapper (buf, size, n, FILE*)
unsigned int GameFwrite = 0x671A61; // game: fwrite wrapper (buf, size, n, FILE*)
unsigned int GameFclose = 0x671F1F; // game: fclose import
unsigned int GameFree = 0x671102;   // game: free import

typedef unsigned int(__cdecl* GameFread_t)(void* buf, unsigned int size, unsigned int n, void* fp);
typedef unsigned int(__cdecl* GameFwrite_t)(const void* buf, unsigned int size, unsigned int n, void* fp);

// Fills ExtTrailer with the addon preset elements and returns the trailer size
// in bytes (0 = nothing to append).
unsigned int __cdecl BuildSaveTrailer()
{
	if (ExtCarCount <= StockStoreCount)
		return 0;
	unsigned int count = ExtCarCount;
	if (count > StoreCapacity)
		count = StoreCapacity;
	unsigned int elements = count - StockStoreCount;
	((unsigned int*)ExtTrailer)[0] = SaveTrailerMagic;
	((unsigned int*)ExtTrailer)[1] = count;
	memcpy(ExtTrailer + SaveTrailerHeaderSize, &ExtCarStore[StockStoreCount], elements * StoreElementSize);
	return SaveTrailerHeaderSize + elements * StoreElementSize;
}

// Reads the trailer from an open .ugd file and merges its elements into
// ExtCarStore by +8 car-type hash; types that no longer exist are skipped.
void __cdecl MergeLoadTrailer(void* fp)
{
	unsigned int header[2];
	if (((GameFread_t)GameFread)(header, 4, 2, fp) != 2)
		return;
	if (header[0] != SaveTrailerMagic)
		return;
	unsigned int count = header[1];
	if (count > StoreCapacity)
		count = StoreCapacity;
	if (count <= StockStoreCount)
		return;
	BYTE element[StoreElementSize];
	for (unsigned int i = StockStoreCount; i < count; i++)
	{
		if (((GameFread_t)GameFread)(element, StoreElementSize, 1, fp) != 1)
			return;
		unsigned int hash = *(unsigned int*)(element + 8);
		for (unsigned int j = 0; j < ExtCarCount; j++)
		{
			if (*(unsigned int*)(ExtCarStore[j] + 8) == hash)
			{
				memcpy(ExtCarStore[j], element, StoreElementSize);
				break;
			}
		}
	}
}

// 0x41DA0C: push esi / call _free in the .ugd writer - appends the trailer
// right after the CRC dword, then replicates the stolen bytes.
void __declspec(naked) SaveTrailerAppendCave()
{
	_asm
	{
		pushad
		call BuildSaveTrailer
		test eax, eax
		jz noTrailer
		push ebp                       // FILE*
		push eax                       // n
		push 1                         // size
		push offset ExtTrailer         // buf
		call dword ptr [GameFwrite]
		add esp, 10h
	noTrailer:
		popad
		push esi                       // stolen: push esi
		call dword ptr [GameFree]      // stolen: call _free
		push 0x41DA12
		retn
	}
}

// 0x41DB19: call _fclose on the .ugd reader's success path - reads the trailer
// while the file is still open (the FILE* was pushed at 0x41DAF9), then
// replicates the stolen call.
void __declspec(naked) SaveTrailerLoadCave()
{
	_asm
	{
		pushad
		mov eax, dword ptr [esp+32]    // FILE* pushed before the hook
		push eax
		call MergeLoadTrailer
		add esp, 4
		popad
		call dword ptr [GameFclose]    // stolen: call _fclose (arg still on stack)
		push 0x41DB1E
		retn
	}
}



void __declspec(naked) DoUnlimiterStuffCodeCave()
{
	// Get count
	_asm mov dword ptr ds : [CarTypeInfoArray] , edi;
	_asm sub edi, 0x0C;
	_asm mov edi, [edi];
	_asm mov CarArraySize, edi;
	_asm mov edi, dword ptr ds : [CarTypeInfoArray] ;
	_asm pushad;

	CarArraySize -= 8;
	CarCount = CarArraySize / 0xC90;
	if (CarCount > 127)
	{
		CarCount = 127;
		CarArraySize = CarCount * 0xC90;
	}
	// Do required stuff

	// Car Type Unlimiter
	injector::WriteMemory<int>(0x4397C4, CarArraySize, true); // sub_4394F0
	injector::WriteMemory<int>(0x4397E4, CarArraySize, true); // sub_4394F0
	injector::WriteMemory<int>(0x43B048, CarArraySize, true); // sub_43AF80
	injector::WriteMemory<int>(0x43B068, CarArraySize, true); // sub_43AF80
	injector::WriteMemory<int>(0x4AC519, CarArraySize, true); // FEPlayerCarDB::DefaultCustomizableCars
	injector::WriteMemory<int>(0x4C32C0, CarArraySize, true); // sub_4C3240
	injector::WriteMemory<int>(0x4C43B5, CarArraySize, true); // sub_4C42D0

	injector::WriteMemory<BYTE>(0x405421, CarCount, true); // sub_4053F0
	injector::WriteMemory<int>(0x439C7A, CarCount, true); // sub_439AA0
	injector::WriteMemory<BYTE>(0x4ABFF3, CarCount, true); // FEPlayerCarDB::FindCustomizableCarByType
	injector::WriteMemory<BYTE>(0x4AC5E1, CarCount, true); // FECarConfig::BuildRide
	injector::WriteMemory<BYTE>(0x4ACCFF, CarCount, true); // FECarConfig::GetCarType
	injector::WriteMemory<BYTE>(0x4B3944, CarCount, true); // sub_4B3940
	injector::WriteMemory<BYTE>(0x4BC1CD, CarCount, true); // QRCarSelectScreen::BuildSelectableCarList
	injector::WriteMemory<BYTE>(0x4BC3FD, CarCount, true); // QRCarSelectScreen::BuildSelectableCarList
	injector::WriteMemory<BYTE>(0x4BC51D, CarCount, true); // QRCarSelectScreen::BuildSelectableCarList
	injector::WriteMemory<BYTE>(0x4C673D, CarCount, true); // MainMenuCarCustomize::BuildSelectableCarList
	injector::WriteMemory<BYTE>(0x4C790A, CarCount, true); // UndergroundSamanthaGiveth::UndergroundSamanthaGiveth
	injector::WriteMemory<BYTE>(0x4C7AB7, CarCount, true); // UndergroundSamanthaGiveth::UndergroundSamanthaGiveth
	injector::WriteMemory<int>(0x4C78FE, CarCount, true); // UndergroundSamanthaGiveth::UndergroundSamanthaGiveth
	injector::WriteMemory<int>(0x4C7A57, CarCount, true); // UndergroundSamanthaGiveth::UndergroundSamanthaGiveth
	injector::WriteMemory<int>(0x4C7AAA, CarCount, true); // UndergroundSamanthaGiveth::UndergroundSamanthaGiveth
	injector::WriteMemory<BYTE>(0x4E0E0D, CarCount, true); // sub_4E0D90
	injector::WriteMemory<BYTE>(0x4E104D, CarCount, true); // sub_4E0D90
	injector::WriteMemory<BYTE>(0x4E1145, CarCount, true); // sub_4E0D90
	injector::WriteMemory<BYTE>(0x4E4908, CarCount, true); // sub_4E4840
	injector::WriteMemory<BYTE>(0x4E49EF, CarCount, true); // sub_4E4840
	injector::WriteMemory<BYTE>(0x504640, CarCount, true); // sub_504630
	injector::WriteMemory<BYTE>(0x50467E, CarCount, true); // sub_504630
	injector::WriteMemory<BYTE>(0x50736F, CarCount, true); // sub_507340
	injector::WriteMemory<BYTE>(0x507479, CarCount, true); // sub_507340
	injector::WriteMemory<BYTE>(0x50D372, CarCount, true); // sub_50D360
	injector::WriteMemory<BYTE>(0x510E84, CarCount, true); // GarageDecalsSlot::GarageDecalsSlot
	injector::WriteMemory<BYTE>(0x510EEC, CarCount, true); // GarageDecalsSlot::GarageDecalsSlot
	injector::WriteMemory<BYTE>(0x511F4E, CarCount, true); // sub_511F00
	injector::WriteMemory<BYTE>(0x57C8F3, CarCount, true); // LoaderCarInfo
	injector::WriteMemory<BYTE>(0x57CED4, CarCount, true); // sub_57CED0
	injector::WriteMemory<BYTE>(0x57CF8D, CarCount, true); // GetCarTypeInfo
	injector::WriteMemory<BYTE>(0x57CFA3, CarCount, true); // sub_57CFA0
	injector::WriteMemory<BYTE>(0x57CFF0, CarCount, true); // sub_57CFA0
	injector::WriteMemory<BYTE>(0x57D4BE, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D52A, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D59A, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D60A, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D67A, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D6EA, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57D747, CarCount, true); // RideInfo::SetStockParts
	injector::WriteMemory<BYTE>(0x57EDAD, CarCount, true); // RideInfo::UpdatePartsEnabled
	injector::WriteMemory<BYTE>(0x57EE17, CarCount, true); // RideInfo::UpdatePartsEnabled
	injector::WriteMemory<BYTE>(0x57EE85, CarCount, true); // RideInfo::UpdatePartsEnabled
	injector::WriteMemory<BYTE>(0x57EEF4, CarCount, true); // RideInfo::UpdatePartsEnabled
	injector::WriteMemory<BYTE>(0x5A3D00, CarCount, true); // sub_5A3C00

	// Add new cars to the hashes table
	for (i = 0; i < CarCount; i++)
	{
		if (IsRacer(i) && HashesCount < 127)
		{
			hashes[HashesCount++] = GetCarTypeNameHash(i);
		}
	}
	TotalRacers = HashesCount - 8;

	// Relocate the hashes table
	injector::WriteMemory(0x4AC564, hashes, true); // sub_4AC550
	injector::WriteMemory(0x4AC6D1, hashes, true); // FECarConfig::BuildRide
	injector::WriteMemory(0x4AC7B3, hashes, true); // PresetCarSlot::FillWithRide
	injector::WriteMemory(0x4AC849, hashes, true); // sub_4AC820

	// Write its element count
	injector::WriteMemory<BYTE>(0x4AC56D, HashesCount, true); // sub_4AC550
	injector::WriteMemory<BYTE>(0x4AC7BC, HashesCount, true); // PresetCarSlot::FillWithRide

	// Sanitize the addon cars appended to the CarTypeInfo array.
	for (i = 0; i < CarCount; i++)
	{
		if (IsRacer(i))
		{
			unsigned int info = (*(DWORD*)CarTypeInfoArray) + i * 0xC90;
			unsigned int crossRef = *(unsigned int*)(info + 0xC50);
			// an unset cross reference (CarTypeInfo+0xC50, the index of the
			// info entry representing the car in the frontend) is dereferenced
			// by the list builders -> point invalid references at the car itself
			if ((int)crossRef < 0 || crossRef >= (unsigned int)CarCount)
			{
				*(unsigned int*)(info + 0xC50) = i;
			}

			// a non-zero unlock flag (CarTypeInfo+0xC58) makes both list
			// builders run their unlock check, which an addon car cannot pass
			// -> clear it so the car is always listed (stock entries untouched)
			if (i >= 35)
			{
				*(unsigned short*)(info + 0xC58) = 0;

				// the preset-ride catalog key (CarTypeInfo+0xC5C) is a shared
				// body-style key: the addon car's own catalog record (which
				// carries no usable parts list) shadows the stock records of
				// every car sharing the key, leaving those with empty preset
				// rides (invisible models, no customize). Give appended cars
				// unique keys so their lookups simply miss and the stock
				// records stay reachable. The original value (the parsed
				// DefaultBasePaint hash) is kept in RealBasePaint and
				// translated back in the game's four BASE_PAINT lookups.
				unsigned int realKey = *(unsigned int*)(info + 0xC5C);
				if (realKey != (0x80000000u | (unsigned int)i))
					RealBasePaint[i] = realKey;
				*(unsigned int*)(info + 0xC5C) = 0x80000000u | (unsigned int)i;
			}
		}
	}

	// ===== Extended preset-car store =====

	// ThumbnailScroller: guard Update against a null current item (crash when
	// more cars are listed than the frontend ships thumbnail slots for)
	injector::MakeJMP(0x4F8A0C, ScrollerCurrentFixCave, true);

	// Restore the real DefaultBasePaint hash in the game's four BASE_PAINT
	// lookups (the pseudo catalog keys stay in CarTypeInfo+0xC5C)
	injector::MakeJMP(0x4ACC5B, BasePaintCave1, true); // FECarConfig::SetDefaults
	injector::MakeJMP(0x4BCAAC, BasePaintCave2, true); // MakeRideStock
	injector::MakeJMP(0x4C2DC8, BasePaintCave3, true); // sub_4C2D20
	injector::MakeJMP(0x4C4294, BasePaintCave4, true); // sub_4C4230

	// FEPlayerCarDB::DefaultCustomizableCars: the global instance builds all
	// racers into ExtCarStore, member instances stay stock
	injector::MakeJMP(0x4AC4B6, CountResetCodeCave, true);
	injector::MakeJMP(0x4AC4DD, DefaultCustomizableCarsCodeCave, true);

	// store count accesses around the game
	injector::MakeJMP(0x4AB57A, CountRead1CodeCave, true);
	injector::MakeJMP(0x4ABE80, CountWrite1CodeCave, true);
	injector::MakeJMP(0x4ABEC4, CountRead2CodeCave, true);
	injector::MakeJMP(0x4ABEDB, CountRead3CodeCave, true);
	injector::MakeJMP(0x4AC071, CountRead4CodeCave, true);
	injector::MakeJMP(0x4AC0B0, CountRead5CodeCave, true);
	injector::MakeJMP(0x4E0DB1, CountRead6CodeCave, true);
	injector::MakeJMP(0x4E0F0A, CountRead7CodeCave, true);

	// store base accesses around the game
	injector::MakeJMP(0x4AB588, StoreBase1CodeCave, true);
	injector::MakeJMP(0x4ABE3F, StoreBase2CodeCave, true);
	injector::MakeJMP(0x4ABED0, StoreBase3CodeCave, true);
	injector::MakeJMP(0x4ABF18, StoreBase4CodeCave, true);
	injector::MakeJMP(0x4AC07D, StoreBase5CodeCave, true);
	injector::MakeJMP(0x4AC0BC, StoreBase6CodeCave, true);
	injector::MakeJMP(0x4AC0FE, StoreBase7CodeCave, true);
	injector::MakeJMP(0x4ABEF4, StoreElem1CodeCave, true);
	injector::MakeJMP(0x4AC02D, StoreElem2CodeCave, true);
	injector::MakeJMP(0x4AC09D, StoreElem3CodeCave, true);
	injector::MakeJMP(0x4AC0DA, StoreElem4CodeCave, true);
	injector::MakeJMP(0x4ABFD6, StoreBase8CodeCave, true);
	injector::MakeJMP(0x4E0DCC, StoreBase9CodeCave, true);

	// store scan limits ("the first 20 cars") -> the full capacity
	injector::WriteMemory<BYTE>(0x4AB596, StoreCapacity, true); // sub_4AB578 repaint stable element loop
	injector::WriteMemory<BYTE>(0x4ABF2D, StoreCapacity, true); // sub_4ABF10 find loop
	injector::WriteMemory<BYTE>(0x4AC111, StoreCapacity, true); // sub_4AC0F0 find loop
	injector::WriteMemory<BYTE>(0x4E0DDD, StoreCapacity, true); // sub_4E0D90 element clamp
	injector::MakeJMP(0x4AC00B, FindLoopBoundCodeCave, true); // sub_4ABFC0 find loop end (crashy not-found path)
	injector::MakeJMP(0x4AC52A, DefaultCarTailCodeCave, true); // DefaultCustomizableCars tail (tolerate not-found)
	injector::MakeJMP(0x4AC7A1, PartsHeadReadCodeCave, true); // preserve the parts-list head null path

	// Savegame preset-ride bridge: mirror ExtCarStore into the pack (all 9
	// savegame pack sites) and merge loaded rides back out of the two loader
	// paths that write the active profile's in-place store
	injector::MakeJMP(0x4AB165, SavePackBridgeCodeCave, true); // sub_4AB120 pack (profile -> savegame blob)
	injector::MakeJMP(0x4AB21D, LoadUnpackBridgeCodeCave, true); // sub_4AB200 unpack (savegame blob -> active profile)
	injector::MakeJMP(0x4CC7AD, LoadSyncBridgeCodeCave, true); // sub_4CC770 career-manager call (temp profile -> stable 0)

	// Savegame trailer: append/restore the addon preset elements in the .ugd
	// savegame files (after the adler CRC, invisible to the stock reader)
	injector::MakeJMP(0x41DA0C, SaveTrailerAppendCave, true); // sub_41D8A0 .ugd writer, after the CRC write
	injector::MakeJMP(0x41DB19, SaveTrailerLoadCave, true); // sub_41DA40 .ugd reader, replaces call _fclose

	// sub_4ABFC0's scan-fail path (0x4ABFF6: xor eax,eax then cmp [eax+0xC50])
	// dereferences counter+0xC50 when the scan fails - instead of patching the
	// shared code region, commit the low pages so the wild read hits zeros and
	// the compare simply fails (original behavior for eax=0)
	VirtualAlloc((void*)0x10000, 0xD0000 - 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	// QR/customize screen store walks: the four label-matching loops
	// (customize-allowed check 0x4BCB00, has-preset check 0x4BCFD0, ride
	// restore 0x4DF880, selection resolve 0x4BD132) hardcode the stock store
	// range 0x75AFA4-0x75F144 (elements 0-19's +0x344 label fields). The
	// migrated store lives in ExtCarStore -> redirect all four walks.
	injector::WriteMemory<unsigned int>(0x4BCB0A, ExtLabelBase, true);
	injector::WriteMemory<unsigned int>(0x4BCFDA, ExtLabelBase, true);
	injector::WriteMemory<unsigned int>(0x4BD133, ExtLabelBase, true);
	injector::WriteMemory<unsigned int>(0x4DF889, ExtLabelBase, true);
	injector::WriteMemory<unsigned int>(0x4BCB1A, ExtLabelBound, true);
	injector::WriteMemory<unsigned int>(0x4BCFEA, ExtLabelBound, true);
	injector::WriteMemory<unsigned int>(0x4BD141, ExtLabelBound, true);
	injector::WriteMemory<unsigned int>(0x4DF89A, ExtLabelBound, true);

	// QRCarSelectScreen::BuildSelectableCarList (0x4BC170):
	injector::WriteMemory(0x4BC177, &ExtCarCount, true); // mov eax, [0x75EE00] -> mov eax, [ExtCarCount]
	injector::WriteMemory(0x4BC2C7, &ExtCarCount, true); // mov ecx, [0x75EE00] -> mov ecx, [ExtCarCount]
	injector::WriteMemory(0x4BC190, ExtStoreBase, true); // mov [ebp-8], 0x75AC60 -> ExtCarStore
	injector::WriteMemory(0x4BC198, ExtStoreBase, true); // cmp eax, 0x75AC60 -> ExtCarStore
	injector::WriteMemory(0x4BC19F, ExtStoreEnd, true); // cmp eax, 0x75EE00 -> end of ExtCarStore

	// MainMenuCarCustomize::BuildSelectableCarList (0x4C66D0):
	injector::WriteMemory(0x4C66D4, &ExtCarCount, true); // mov eax, [0x75EE00] -> mov eax, [ExtCarCount]
	injector::WriteMemory(0x4C6843, &ExtCarCount, true); // mov ecx, [0x75EE00] -> mov ecx, [ExtCarCount]
	injector::WriteMemory(0x4C66F3, ExtStoreBase, true); // mov ebx, 0x75AC60 -> ExtCarStore (BB + imm32)
	injector::WriteMemory(0x4C6702, ExtStoreBase, true); // cmp ebx, 0x75AC60 -> ExtCarStore
	injector::WriteMemory(0x4C670A, ExtStoreEnd, true); // cmp ebx, 0x75EE00 -> end of ExtCarStore

	// Continue
	_asm popad;
	_asm push 0x57C8CE;
	_asm retn;
}

int BogusCarPart;

void __declspec(naked) CrashWorkaroundCodeCave()
{
	_asm
	{
		mov ecx, dword ptr ds:[ecx+0x490]
		test ecx, ecx
		jz workaround

		usualthing:
			mov dword ptr ds: [BogusCarPart], ecx
			jmp caveexit

		workaround:
			mov ecx, dword ptr ds : [BogusCarPart]

		caveexit:
			push 0x56A36D
			retn
	}
}

void Init()
{
	CIniReader Settings("NFSUUnlimiterSettings.ini");

	// Main
	SortCarsByID = Settings.ReadInteger("Main", "SortCarsByID", 0) == 1;
	// Fixes
	DisappearingWheelsFix = Settings.ReadInteger("Fixes", "DisappearingWheelsFix", 1) == 1;

	// Count Cars Automatically
	injector::MakeJMP(0x57C8C8, DoUnlimiterStuffCodeCave, true);


	// Crash Workaround
	injector::MakeJMP(0x56A367, CrashWorkaroundCodeCave, true);

	// Fix Invisible Wheels
	if (DisappearingWheelsFix)
	{
		injector::WriteMemory<unsigned char>(0x5696E4, 0x01, true);
	}

	// Sort the cars by their ID instead of special flags
	if (SortCarsByID)
	{
		injector::WriteMemory<unsigned char>(0x4C2BC0, 0xC3, true);
	}

}
