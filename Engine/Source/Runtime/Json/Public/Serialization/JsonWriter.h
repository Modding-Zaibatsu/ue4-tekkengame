// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policies/PrettyJsonPrintPolicy.h"
#include "Dom/JsonValue.h"

/**
 * Template for Json writers.
 *
 * @param CharType The type of characters to print, i.e. TCHAR or ANSICHAR.
 * @param PrintPolicy The print policy to use when writing the output string (default = TPrettyJsonPrintPolicy).
 */
template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType> >
class TJsonWriter
{
public:

	static TSharedRef< TJsonWriter > Create( FArchive* const Stream, int32 InitialIndentLevel = 0 )
	{
		return MakeShareable( new TJsonWriter< CharType, PrintPolicy >( Stream, InitialIndentLevel ) );
	}

public:

	virtual ~TJsonWriter() { }

	FORCEINLINE int32 GetIndentLevel() const { return IndentLevel; }

	void WriteObjectStart()
	{
		check(CanWriteObjectWithoutIdentifier());
		if (PreviousTokenWritten != EJsonToken::None )
		{
			WriteCommaIfNeeded();
		}

		if ( PreviousTokenWritten != EJsonToken::None )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}

		PrintPolicy::WriteChar(Stream, CharType('{'));
		++IndentLevel;
		Stack.Push( EJson::Object );
		PreviousTokenWritten = EJsonToken::CurlyOpen;
	}

	void WriteObjectStart( const FString& Identifier )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteLineTerminator(Stream);
		PrintPolicy::WriteTabs(Stream, IndentLevel);
		PrintPolicy::WriteChar(Stream, CharType('{'));
		++IndentLevel;
		Stack.Push( EJson::Object );
		PreviousTokenWritten = EJsonToken::CurlyOpen;
	}

	void WriteObjectEnd()
	{
		check( Stack.Top() == EJson::Object );

		PrintPolicy::WriteLineTerminator(Stream);

		--IndentLevel;
		PrintPolicy::WriteTabs(Stream, IndentLevel);
		PrintPolicy::WriteChar(Stream, CharType('}'));
		Stack.Pop();
		PreviousTokenWritten = EJsonToken::CurlyClose;
	}

	void WriteArrayStart()
	{
		check(CanWriteValueWithoutIdentifier());
		if ( PreviousTokenWritten != EJsonToken::None )
		{
			WriteCommaIfNeeded();
		}

		if ( PreviousTokenWritten != EJsonToken::None )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}

		PrintPolicy::WriteChar(Stream, CharType('['));
		++IndentLevel;
		Stack.Push( EJson::Array );
		PreviousTokenWritten = EJsonToken::SquareOpen;
	}

	void WriteArrayStart( const FString& Identifier )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace( Stream );
		PrintPolicy::WriteChar(Stream, CharType('['));
		++IndentLevel;
		Stack.Push( EJson::Array );
		PreviousTokenWritten = EJsonToken::SquareOpen;
	}

	void WriteArrayEnd()
	{
		check( Stack.Top() == EJson::Array );

		--IndentLevel;
		if ( PreviousTokenWritten == EJsonToken::SquareClose || PreviousTokenWritten == EJsonToken::CurlyClose || PreviousTokenWritten == EJsonToken::String )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else if ( PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteSpace( Stream );
		}

		PrintPolicy::WriteChar(Stream, CharType(']'));
		Stack.Pop();
		PreviousTokenWritten = EJsonToken::SquareClose;
	}

	void WriteValue( const FString& Identifier, const bool Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteBoolValue( Value );
		PreviousTokenWritten = Value ? EJsonToken::True : EJsonToken::False;
	}

	void WriteValue( const FString& Identifier, const double Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteNumberValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const FString& Identifier, const int32 Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteIntegerValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const FString& Identifier, const int64 Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteIntegerValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const FString& Identifier, const FString& Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteStringValue( Value );
		PreviousTokenWritten = EJsonToken::String;
	}

	void WriteValue( const FString& Identifier, const TCHAR* Value )
	{
		WriteValue(Identifier, FString(Value));
	}

	// WARNING: THIS IS DANGEROUS. Use this only if you know for a fact that the Value is valid JSON!
	// Use this to insert the results of a different JSON Writer in.
	void WriteRawJSONValue( const FString& Identifier, const FString& Value )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		PrintPolicy::WriteString(Stream, Value);
		PreviousTokenWritten = EJsonToken::String;
	}

	void WriteNull( const FString& Identifier )
	{
		check( Stack.Top() == EJson::Object );
		WriteIdentifier( Identifier );

		PrintPolicy::WriteSpace(Stream);
		WriteNullValue();
		PreviousTokenWritten = EJsonToken::Null;
	}

	void WriteValue( const bool Value )
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();

		if ( PreviousTokenWritten != EJsonToken::True && PreviousTokenWritten != EJsonToken::False && PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else
		{
			PrintPolicy::WriteSpace( Stream );
		}

		WriteBoolValue( Value );
		PreviousTokenWritten = Value ? EJsonToken::True : EJsonToken::False;
	}

	void WriteValue( const double Value )
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();

		if ( PreviousTokenWritten != EJsonToken::Number && PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else
		{
			PrintPolicy::WriteSpace( Stream );
		}

		WriteNumberValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const int32 Value )
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();

		if ( PreviousTokenWritten != EJsonToken::Number && PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else
		{
			PrintPolicy::WriteSpace( Stream );
		}

		WriteIntegerValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const int64 Value )
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();

		if ( PreviousTokenWritten != EJsonToken::Number && PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else
		{
			PrintPolicy::WriteSpace( Stream );
		}

		WriteIntegerValue( Value );
		PreviousTokenWritten = EJsonToken::Number;
	}

	void WriteValue( const FString& Value )
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();
		PrintPolicy::WriteLineTerminator(Stream);
		PrintPolicy::WriteTabs(Stream, IndentLevel);
		WriteStringValue( Value );
		PreviousTokenWritten = EJsonToken::String;
	}

	void WriteValue( const TCHAR* Value )
	{
		WriteValue(FString(Value));
	}

	void WriteNull()
	{
		check(CanWriteValueWithoutIdentifier());
		WriteCommaIfNeeded();

		if ( PreviousTokenWritten != EJsonToken::Null && PreviousTokenWritten != EJsonToken::SquareOpen )
		{
			PrintPolicy::WriteLineTerminator(Stream);
			PrintPolicy::WriteTabs(Stream, IndentLevel);
		}
		else
		{
			PrintPolicy::WriteSpace( Stream );
		}

		WriteNullValue();
		PreviousTokenWritten = EJsonToken::Null;
	}

	virtual bool Close()
	{
		return ( PreviousTokenWritten == EJsonToken::None || 
				 PreviousTokenWritten == EJsonToken::CurlyClose  || 
				 PreviousTokenWritten == EJsonToken::SquareClose ) 
				&& Stack.Num() == 0;
	}

	/**
	 * WriteValue("Foo", Bar) should be equivalent to WriteIdentifierPrefix("Foo"), WriteValue(Bar)
	 */
	void WriteIdentifierPrefix(const FString& Identifier)
	{
		check(Stack.Top() == EJson::Object);
		WriteIdentifier(Identifier);
		PrintPolicy::WriteSpace(Stream);
		PreviousTokenWritten = EJsonToken::Identifier;
	}

protected:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InStream An archive containing the input.
	 * @param InitialIndentLevel The initial indentation level.
	 */
	TJsonWriter( FArchive* const InStream, int32 InitialIndentLevel )
		: Stream( InStream )
		, Stack()
		, PreviousTokenWritten(EJsonToken::None)
		, IndentLevel(InitialIndentLevel)
	{ }

protected:

	FORCEINLINE bool CanWriteValueWithoutIdentifier() const 
	{ 
		return Stack.Num() <= 0 || Stack.Top() == EJson::Array || PreviousTokenWritten == EJsonToken::Identifier;
	}

	FORCEINLINE bool CanWriteObjectWithoutIdentifier() const
	{
		return Stack.Num() <= 0 || Stack.Top() == EJson::Array || PreviousTokenWritten == EJsonToken::Identifier || PreviousTokenWritten == EJsonToken::Colon;
	}

	FORCEINLINE void WriteCommaIfNeeded()
	{
		if ( PreviousTokenWritten != EJsonToken::CurlyOpen && PreviousTokenWritten != EJsonToken::SquareOpen && PreviousTokenWritten != EJsonToken::Identifier)
		{
			PrintPolicy::WriteChar(Stream, CharType(','));
		}
	}

	FORCEINLINE void WriteIdentifier( const FString& Identifier )
	{
		WriteCommaIfNeeded();
		PrintPolicy::WriteLineTerminator(Stream);

		PrintPolicy::WriteTabs(Stream, IndentLevel);
		WriteStringValue( Identifier );
		PrintPolicy::WriteChar(Stream, CharType(':'));
	}

	FORCEINLINE void WriteBoolValue( const bool Value )
	{
		PrintPolicy::WriteString(Stream, Value ? TEXT("true") : TEXT("false"));
	}

	FORCEINLINE void WriteNumberValue( const double Value)
	{
		// Specify 17 significant digits, the most that can ever be useful from a double
		// In particular, this ensures large integers are written correctly
		PrintPolicy::WriteString(Stream, FString::Printf(TEXT("%.17g"), Value));
	}

	FORCEINLINE void WriteIntegerValue( const int64 Value)
	{
		PrintPolicy::WriteString(Stream, FString::Printf(TEXT("%lld"), Value));
	}

	FORCEINLINE void WriteNullValue()
	{
		PrintPolicy::WriteString(Stream, TEXT("null"));
	}

	virtual void WriteStringValue( const FString& String )
	{
		FString OutString;
		OutString += TEXT("\"");
		for (const TCHAR* Char = *String; *Char != TCHAR('\0'); ++Char)
		{
			switch (*Char)
			{
			case TCHAR('\\'): OutString += TEXT("\\\\"); break;
			case TCHAR('\n'): OutString += TEXT("\\n"); break;
			case TCHAR('\t'): OutString += TEXT("\\t"); break;
			case TCHAR('\b'): OutString += TEXT("\\b"); break;
			case TCHAR('\f'): OutString += TEXT("\\f"); break;
			case TCHAR('\r'): OutString += TEXT("\\r"); break;
			case TCHAR('\"'): OutString += TEXT("\\\""); break;
			default: OutString += *Char;
			}
		}
		OutString += TEXT("\"");
		PrintPolicy::WriteString(Stream, OutString);
	}

protected:

	FArchive* const Stream;
	TArray<EJson> Stack;
	EJsonToken PreviousTokenWritten;
	int32 IndentLevel;
};


template <class PrintPolicy = TPrettyJsonPrintPolicy<TCHAR>>
class TJsonStringWriter
	: public TJsonWriter<TCHAR, PrintPolicy>
{
public:

	static TSharedRef<TJsonStringWriter> Create( FString* const InStream, int32 InitialIndent = 0 )
	{
		return MakeShareable(new TJsonStringWriter(InStream, InitialIndent));
	}

public:

	virtual ~TJsonStringWriter()
	{
		check(this->Stream->Close());
		delete this->Stream;
	}

	virtual bool Close() override
	{
		FString Out;

		for (int32 i = 0; i < Bytes.Num(); i+=sizeof(TCHAR))
		{
			TCHAR* Char = static_cast<TCHAR*>(static_cast<void*>(&Bytes[i]));
			Out += *Char;
		}

		*OutString = Out;

		return TJsonWriter<TCHAR, PrintPolicy>::Close();
	}

protected:

	TJsonStringWriter( FString* const InOutString, int32 InitialIndent )
		: TJsonWriter<TCHAR, PrintPolicy>(new FMemoryWriter(Bytes), InitialIndent)
		, Bytes()
		, OutString(InOutString)
	{ }

private:

	TArray<uint8> Bytes;
	FString* OutString;
};


template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
class TJsonWriterFactory
{
public:

	static TSharedRef<TJsonWriter<CharType, PrintPolicy>> Create( FArchive* const Stream, int32 InitialIndent = 0 )
	{
		return TJsonWriter< CharType, PrintPolicy >::Create(Stream, InitialIndent);
	}

	static TSharedRef<TJsonWriter<TCHAR, PrintPolicy>> Create( FString* const Stream, int32 InitialIndent = 0 )
	{
		return StaticCastSharedRef<TJsonWriter<TCHAR, PrintPolicy>>(TJsonStringWriter<PrintPolicy>::Create(Stream, InitialIndent));
	}
};
