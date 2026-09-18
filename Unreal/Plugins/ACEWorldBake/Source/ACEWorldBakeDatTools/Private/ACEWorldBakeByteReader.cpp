#include "ACEWorldBakeByteReader.h"

FACEWorldBakeByteReader::FACEWorldBakeByteReader(TByteArrayPtr InData)
: Data(InData)
, Position(0)
, TotalSizeInBytes(InData->Num())
{
}

FString FACEWorldBakeByteReader::ReadString()
{
	uint32 Length = ReadInt16();
	if (0xFFFF == Length)
	{
		Length = ReadInt32();
	}

	const uint8* DataBlob = ReadBlob(Length);

	Align();

	TArray<ANSICHAR> AsAnsiCharArray;
	AsAnsiCharArray.Append(reinterpret_cast<const ANSICHAR*>(DataBlob), Length);
	AsAnsiCharArray.Add('\0');

	FString AsString = FString(AsAnsiCharArray.GetData());
	check(AsString.Len() == Length);
	return AsString;
}
