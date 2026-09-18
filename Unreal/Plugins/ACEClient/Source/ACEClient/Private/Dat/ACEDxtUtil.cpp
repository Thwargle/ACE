#include "Dat/ACEDxtUtil.h"

namespace ACEDxtUtil
{
	static void ConvertRgb565ToRgb888(uint16 Color, uint8& R, uint8& G, uint8& B)
	{
		const int32 Temp = (Color >> 11) * 255 + 16;
		R = static_cast<uint8>((Temp / 32 + Temp) / 32);
		const int32 TempG = ((Color & 0x07E0) >> 5) * 255 + 32;
		G = static_cast<uint8>((TempG / 64 + TempG) / 64);
		const int32 TempB = (Color & 0x001F) * 255 + 16;
		B = static_cast<uint8>((TempB / 32 + TempB) / 32);
	}

	static void WritePixel(TArray<uint8>& Image, int32 Width, int32 Height, int32 Px, int32 Py, uint8 R, uint8 G, uint8 B, uint8 A)
	{
		if (Px < 0 || Py < 0 || Px >= Width || Py >= Height)
		{
			return;
		}
		const int32 Offset = ((Py * Width) + Px) * 4;
		Image[Offset] = R;
		Image[Offset + 1] = G;
		Image[Offset + 2] = B;
		Image[Offset + 3] = A;
	}

	void DecompressDxt1(const TArray<uint8>& Src, int32 Width, int32 Height, TArray<uint8>& OutRGBA)
	{
		OutRGBA.SetNumZeroed(Width * Height * 4);
		int32 Pos = 0;
		const int32 BlockCountX = (Width + 3) / 4;
		const int32 BlockCountY = (Height + 3) / 4;
		auto ReadU16 = [&]() -> uint16
		{
			if (Pos + 2 > Src.Num())
			{
				return 0;
			}
			const uint16 V = static_cast<uint16>(Src[Pos] | (Src[Pos + 1] << 8));
			Pos += 2;
			return V;
		};
		auto ReadU32 = [&]() -> uint32
		{
			if (Pos + 4 > Src.Num())
			{
				return 0;
			}
			const uint32 V = Src[Pos] | (Src[Pos + 1] << 8) | (Src[Pos + 2] << 16) | (Src[Pos + 3] << 24);
			Pos += 4;
			return V;
		};

		for (int32 By = 0; By < BlockCountY; ++By)
		{
			for (int32 Bx = 0; Bx < BlockCountX; ++Bx)
			{
				const uint16 C0 = ReadU16();
				const uint16 C1 = ReadU16();
				uint8 R0, G0, B0, R1, G1, B1;
				ConvertRgb565ToRgb888(C0, R0, G0, B0);
				ConvertRgb565ToRgb888(C1, R1, G1, B1);
				const uint32 Lookup = ReadU32();

				for (int32 BlockY = 0; BlockY < 4; ++BlockY)
				{
					for (int32 BlockX = 0; BlockX < 4; ++BlockX)
					{
						uint8 R = 0, G = 0, B = 0, A = 255;
						const uint32 Index = (Lookup >> (2 * (4 * BlockY + BlockX))) & 0x03;
						if (C0 > C1)
						{
							switch (Index)
							{
							case 0: R = R0; G = G0; B = B0; break;
							case 1: R = R1; G = G1; B = B1; break;
							case 2:
								R = static_cast<uint8>((2 * R0 + R1) / 3);
								G = static_cast<uint8>((2 * G0 + G1) / 3);
								B = static_cast<uint8>((2 * B0 + B1) / 3);
								break;
							case 3:
								R = static_cast<uint8>((R0 + 2 * R1) / 3);
								G = static_cast<uint8>((G0 + 2 * G1) / 3);
								B = static_cast<uint8>((B0 + 2 * B1) / 3);
								break;
							}
						}
						else
						{
							switch (Index)
							{
							case 0: R = R0; G = G0; B = B0; break;
							case 1: R = R1; G = G1; B = B1; break;
							case 2:
								R = static_cast<uint8>((R0 + R1) / 2);
								G = static_cast<uint8>((G0 + G1) / 2);
								B = static_cast<uint8>((B0 + B1) / 2);
								break;
							case 3: R = 0; G = 0; B = 0; A = 0; break;
							}
						}
						WritePixel(OutRGBA, Width, Height, (Bx << 2) + BlockX, (By << 2) + BlockY, R, G, B, A);
					}
				}
			}
		}
	}

	void DecompressDxt3(const TArray<uint8>& Src, int32 Width, int32 Height, TArray<uint8>& OutRGBA)
	{
		OutRGBA.SetNumZeroed(Width * Height * 4);
		int32 Pos = 0;
		const int32 BlockCountX = (Width + 3) / 4;
		const int32 BlockCountY = (Height + 3) / 4;
		auto ReadByte = [&]() -> uint8
		{
			return (Pos < Src.Num()) ? Src[Pos++] : 0;
		};
		auto ReadU16 = [&]() -> uint16
		{
			const uint16 V = static_cast<uint16>(ReadByte() | (ReadByte() << 8));
			return V;
		};
		auto ReadU32 = [&]() -> uint32
		{
			return ReadByte() | (ReadByte() << 8) | (ReadByte() << 16) | (ReadByte() << 24);
		};

		for (int32 By = 0; By < BlockCountY; ++By)
		{
			for (int32 Bx = 0; Bx < BlockCountX; ++Bx)
			{
				uint8 AlphaBytes[8];
				for (int32 i = 0; i < 8; ++i)
				{
					AlphaBytes[i] = ReadByte();
				}
				const uint16 C0 = ReadU16();
				const uint16 C1 = ReadU16();
				uint8 R0, G0, B0, R1, G1, B1;
				ConvertRgb565ToRgb888(C0, R0, G0, B0);
				ConvertRgb565ToRgb888(C1, R1, G1, B1);
				const uint32 Lookup = ReadU32();

				int32 AlphaIndex = 0;
				for (int32 BlockY = 0; BlockY < 4; ++BlockY)
				{
					for (int32 BlockX = 0; BlockX < 4; ++BlockX)
					{
						uint8 R = 0, G = 0, B = 0, A = 0;
						const uint32 Index = (Lookup >> (2 * (4 * BlockY + BlockX))) & 0x03;
						const uint8 Ab = AlphaBytes[AlphaIndex / 2];
						A = ((AlphaIndex & 1) == 0)
							? static_cast<uint8>(((Ab & 0x0F) << 4) | (Ab & 0x0F))
							: static_cast<uint8>((Ab & 0xF0) | ((Ab & 0xF0) >> 4));
						++AlphaIndex;

						switch (Index)
						{
						case 0: R = R0; G = G0; B = B0; break;
						case 1: R = R1; G = G1; B = B1; break;
						case 2:
							R = static_cast<uint8>((2 * R0 + R1) / 3);
							G = static_cast<uint8>((2 * G0 + G1) / 3);
							B = static_cast<uint8>((2 * B0 + B1) / 3);
							break;
						case 3:
							R = static_cast<uint8>((R0 + 2 * R1) / 3);
							G = static_cast<uint8>((G0 + 2 * G1) / 3);
							B = static_cast<uint8>((B0 + 2 * B1) / 3);
							break;
						}
						WritePixel(OutRGBA, Width, Height, (Bx << 2) + BlockX, (By << 2) + BlockY, R, G, B, A);
					}
				}
			}
		}
	}

	void DecompressDxt5(const TArray<uint8>& Src, int32 Width, int32 Height, TArray<uint8>& OutRGBA)
	{
		OutRGBA.SetNumZeroed(Width * Height * 4);
		int32 Pos = 0;
		const int32 BlockCountX = (Width + 3) / 4;
		const int32 BlockCountY = (Height + 3) / 4;
		auto ReadByte = [&]() -> uint8
		{
			return (Pos < Src.Num()) ? Src[Pos++] : 0;
		};
		auto ReadU16 = [&]() -> uint16
		{
			return static_cast<uint16>(ReadByte() | (ReadByte() << 8));
		};
		auto ReadU32 = [&]() -> uint32
		{
			return ReadByte() | (ReadByte() << 8) | (ReadByte() << 16) | (ReadByte() << 24);
		};

		for (int32 By = 0; By < BlockCountY; ++By)
		{
			for (int32 Bx = 0; Bx < BlockCountX; ++Bx)
			{
				const uint8 Alpha0 = ReadByte();
				const uint8 Alpha1 = ReadByte();
				uint64 AlphaMask = 0;
				for (int32 i = 0; i < 6; ++i)
				{
					AlphaMask |= (static_cast<uint64>(ReadByte()) << (8 * i));
				}
				const uint16 C0 = ReadU16();
				const uint16 C1 = ReadU16();
				uint8 R0, G0, B0, R1, G1, B1;
				ConvertRgb565ToRgb888(C0, R0, G0, B0);
				ConvertRgb565ToRgb888(C1, R1, G1, B1);
				const uint32 Lookup = ReadU32();

				for (int32 BlockY = 0; BlockY < 4; ++BlockY)
				{
					for (int32 BlockX = 0; BlockX < 4; ++BlockX)
					{
						uint8 R = 0, G = 0, B = 0, A = 255;
						const uint32 Index = (Lookup >> (2 * (4 * BlockY + BlockX))) & 0x03;
						const uint32 AIndex = static_cast<uint32>((AlphaMask >> (3 * (4 * BlockY + BlockX))) & 0x07);
						if (AIndex == 0)
						{
							A = Alpha0;
						}
						else if (AIndex == 1)
						{
							A = Alpha1;
						}
						else if (Alpha0 > Alpha1)
						{
							A = static_cast<uint8>(((8 - AIndex) * Alpha0 + (AIndex - 1) * Alpha1) / 7);
						}
						else
						{
							switch (AIndex)
							{
							case 6: A = 0; break;
							case 7: A = 255; break;
							default: A = static_cast<uint8>(((6 - AIndex) * Alpha0 + (AIndex - 1) * Alpha1) / 5); break;
							}
						}

						switch (Index)
						{
						case 0: R = R0; G = G0; B = B0; break;
						case 1: R = R1; G = G1; B = B1; break;
						case 2:
							R = static_cast<uint8>((2 * R0 + R1) / 3);
							G = static_cast<uint8>((2 * G0 + G1) / 3);
							B = static_cast<uint8>((2 * B0 + B1) / 3);
							break;
						case 3:
							R = static_cast<uint8>((R0 + 2 * R1) / 3);
							G = static_cast<uint8>((G0 + 2 * G1) / 3);
							B = static_cast<uint8>((B0 + 2 * B1) / 3);
							break;
						}
						WritePixel(OutRGBA, Width, Height, (Bx << 2) + BlockX, (By << 2) + BlockY, R, G, B, A);
					}
				}
			}
		}
	}

	static uint16 ToRgb565(const FColor& C)
	{
		return static_cast<uint16>(((C.R >> 3) << 11) | ((C.G >> 2) << 5) | (C.B >> 3));
	}

	static void SampleBlock(const TArray<FColor>& Pixels, int32 Width, int32 Height,
		int32 Bx, int32 By, FColor Out[16])
	{
		for (int32 Py = 0; Py < 4; ++Py)
		{
			const int32 Y = FMath::Min(By + Py, Height - 1);
			for (int32 Px = 0; Px < 4; ++Px)
			{
				const int32 X = FMath::Min(Bx + Px, Width - 1);
				Out[Py * 4 + Px] = Pixels[Y * Width + X];
			}
		}
	}

	static void EncodeDxt1Block(const FColor Pixels[16], uint8 Out[8], bool bForceOpaque)
	{
		int32 MinL = 4 * 255, MaxL = -1, MinI = 0, MaxI = 0;
		for (int32 i = 0; i < 16; ++i)
		{
			const int32 L = static_cast<int32>(Pixels[i].R) + Pixels[i].G + Pixels[i].B;
			if (L < MinL) { MinL = L; MinI = i; }
			if (L > MaxL) { MaxL = L; MaxI = i; }
		}
		uint16 C0 = ToRgb565(Pixels[MaxI]);
		uint16 C1 = ToRgb565(Pixels[MinI]);
		if (bForceOpaque && C0 == C1)
		{
			C0 = static_cast<uint16>(C0 == 0 ? 1 : C0);
		}
		if (bForceOpaque)
		{
			if (C0 < C1)
			{
				Swap(C0, C1);
			}
		}
		else if (C0 > C1)
		{
			Swap(C0, C1);
		}

		uint8 PalR[4], PalG[4], PalB[4];
		ConvertRgb565ToRgb888(C0, PalR[0], PalG[0], PalB[0]);
		ConvertRgb565ToRgb888(C1, PalR[1], PalG[1], PalB[1]);
		if (C0 > C1)
		{
			PalR[2] = static_cast<uint8>((2 * PalR[0] + PalR[1]) / 3);
			PalG[2] = static_cast<uint8>((2 * PalG[0] + PalG[1]) / 3);
			PalB[2] = static_cast<uint8>((2 * PalB[0] + PalB[1]) / 3);
			PalR[3] = static_cast<uint8>((PalR[0] + 2 * PalR[1]) / 3);
			PalG[3] = static_cast<uint8>((PalG[0] + 2 * PalG[1]) / 3);
			PalB[3] = static_cast<uint8>((PalB[0] + 2 * PalB[1]) / 3);
		}
		else
		{
			PalR[2] = static_cast<uint8>((PalR[0] + PalR[1]) / 2);
			PalG[2] = static_cast<uint8>((PalG[0] + PalG[1]) / 2);
			PalB[2] = static_cast<uint8>((PalB[0] + PalB[1]) / 2);
			PalR[3] = 0; PalG[3] = 0; PalB[3] = 0;
		}

		uint32 Lookup = 0;
		for (int32 i = 0; i < 16; ++i)
		{
			int32 Best = 0;
			int32 BestD = MAX_int32;
			const int32 MaxIdx = (C0 > C1) ? 4 : 3;
			for (int32 P = 0; P < MaxIdx; ++P)
			{
				const int32 Dr = static_cast<int32>(Pixels[i].R) - PalR[P];
				const int32 Dg = static_cast<int32>(Pixels[i].G) - PalG[P];
				const int32 Db = static_cast<int32>(Pixels[i].B) - PalB[P];
				const int32 D = Dr * Dr + Dg * Dg + Db * Db;
				if (D < BestD)
				{
					BestD = D;
					Best = P;
				}
			}
			Lookup |= static_cast<uint32>(Best) << (2 * i);
		}
		Out[0] = static_cast<uint8>(C0);
		Out[1] = static_cast<uint8>(C0 >> 8);
		Out[2] = static_cast<uint8>(C1);
		Out[3] = static_cast<uint8>(C1 >> 8);
		Out[4] = static_cast<uint8>(Lookup);
		Out[5] = static_cast<uint8>(Lookup >> 8);
		Out[6] = static_cast<uint8>(Lookup >> 16);
		Out[7] = static_cast<uint8>(Lookup >> 24);
	}

	static void EncodeDxt5AlphaBlock(const FColor Pixels[16], uint8 Out[8])
	{
		uint8 MinA = 255, MaxA = 0;
		for (int32 i = 0; i < 16; ++i)
		{
			MinA = FMath::Min(MinA, Pixels[i].A);
			MaxA = FMath::Max(MaxA, Pixels[i].A);
		}
		if (MaxA == MinA)
		{
			Out[0] = MaxA;
			Out[1] = MaxA;
			FMemory::Memzero(Out + 2, 6);
			return;
		}
		Out[0] = MaxA;
		Out[1] = MinA;
		uint8 Pal[8];
		Pal[0] = MaxA;
		Pal[1] = MinA;
		for (int32 i = 1; i <= 6; ++i)
		{
			Pal[i + 1] = static_cast<uint8>(((7 - i) * MaxA + i * MinA) / 7);
		}
		uint64 Bits = 0;
		for (int32 i = 0; i < 16; ++i)
		{
			int32 Best = 0;
			int32 BestD = 255;
			for (int32 P = 0; P < 8; ++P)
			{
				const int32 D = FMath::Abs(static_cast<int32>(Pixels[i].A) - Pal[P]);
				if (D < BestD)
				{
					BestD = D;
					Best = P;
				}
			}
			Bits |= static_cast<uint64>(Best) << (3 * i);
		}
		for (int32 i = 0; i < 6; ++i)
		{
			Out[2 + i] = static_cast<uint8>(Bits >> (8 * i));
		}
	}

	void CompressDxt1(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& Out)
	{
		const int32 BxCount = (Width + 3) / 4;
		const int32 ByCount = (Height + 3) / 4;
		Out.SetNumUninitialized(BxCount * ByCount * 8);
		int32 Pos = 0;
		FColor Block[16];
		for (int32 By = 0; By < ByCount; ++By)
		{
			for (int32 Bx = 0; Bx < BxCount; ++Bx)
			{
				SampleBlock(Pixels, Width, Height, Bx * 4, By * 4, Block);
				EncodeDxt1Block(Block, Out.GetData() + Pos, /*bForceOpaque*/ true);
				Pos += 8;
			}
		}
	}

	void CompressDxt5(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& Out)
	{
		const int32 BxCount = (Width + 3) / 4;
		const int32 ByCount = (Height + 3) / 4;
		Out.SetNumUninitialized(BxCount * ByCount * 16);
		int32 Pos = 0;
		FColor Block[16];
		for (int32 By = 0; By < ByCount; ++By)
		{
			for (int32 Bx = 0; Bx < BxCount; ++Bx)
			{
				SampleBlock(Pixels, Width, Height, Bx * 4, By * 4, Block);
				EncodeDxt5AlphaBlock(Block, Out.GetData() + Pos);
				EncodeDxt1Block(Block, Out.GetData() + Pos + 8, /*bForceOpaque*/ true);
				Pos += 16;
			}
		}
	}
}
