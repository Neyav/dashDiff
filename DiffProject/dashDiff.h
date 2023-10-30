#pragma once

#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>


#define THREADCOUNT 10

namespace dashDiff
{

	// Stores all the information about our character matching for finding similar blocks of text.
	struct differencesReport
	{
		size_t oldFileSize;
		size_t newFileSize;
		size_t deletedCharacters;
		size_t insertedCharacters;
		size_t sameCharacters;
	};

	class characterRange
	{
	public:
		char* start;
		char* reference;
		char* end;

		char* min;
		char* max;

		size_t sizeofRange(void)
		{
			return end - start;
		}

		// Override > < and == operators for integration into other STL containers.
		bool operator>(const characterRange& other) const
		{
			return (end - start) > (other.end - other.start);
		}
		bool operator<(const characterRange& other) const
		{
			return (end - start) < (other.end - other.start);
		}
		bool operator==(const characterRange& other) const
		{
			return (end - start) == (other.end - other.start);
		}
	};

	struct dualRange
	{
		characterRange oldRange;
		characterRange newRange;
		size_t rangeSize;

		bool operator>(const dualRange& other) const
		{
			return oldRange.start > other.oldRange.start;
		}
		bool operator<(const dualRange& other) const
		{
			return oldRange.start < other.oldRange.start;
		}
		bool operator==(const dualRange& other) const
		{
			return oldRange.start == other.oldRange.start;
		}
	};

	class fileByteBuffer
	{
	public:
		// We're allocating a SINGLE char buffer to store the file in, and then we're going to reference it with pointers.
		std::vector<characterRange> pointerBuffer;

		void add(char* reference, char* start, char* end, char* min, char* max)
		{
			characterRange aChar;
			aChar.reference = reference;
			aChar.start = start;
			aChar.end = end;
			aChar.min = min;
			aChar.max = max;

			pointerBuffer.push_back(aChar);
		}

		// Overrides for > and < and == operators
		// These are for integration into other STL containers if we go that route.
		bool operator>(const fileByteBuffer& other) const
		{
			return pointerBuffer.size() > other.pointerBuffer.size();
		}
		bool operator<(const fileByteBuffer& other) const
		{
			return pointerBuffer.size() < other.pointerBuffer.size();
		}
		bool operator==(const fileByteBuffer& other) const
		{
			return pointerBuffer.size() == other.pointerBuffer.size();
		}
	};

	class threadRequest
	{
	private:
		inline static 	std::mutex threadRequestMutex;
	public:
		int threadId;

		threadRequest()
		{
			threadId = -1;

			// Making sure that for the lifetime of this object, no other thread can request a thread.
			threadRequestMutex.lock();
		}

		~threadRequest()
		{
			threadRequestMutex.unlock();
		}
	};

	class dashDiff
	{
	private:
		// File stream handles for comparison.
		std::fstream oldFile;
		std::fstream newFile;

		char* oldFileBuffer;
		char* newFileBuffer;
		size_t oldFileBufferSize;
		size_t newFileBufferSize;

		fileByteBuffer oldFileBufferArray[256];
		fileByteBuffer newFileBufferArray[256];

		std::vector<dualRange> rangeVector;
		std::mutex rangeVectorMutex;
		std::mutex bufferMutex;

		bool threadActive[THREADCOUNT];
		int threadPercent[THREADCOUNT];

		differencesReport report;

		threadRequest* threadDistributer(void);
		bool threadsActive(void);
		bool valuesOverlap(characterRange& a, characterRange& b);
		void removeWeakOverlaps(void);
		void removeOverlapEntry(int* it);
		void reduceOverlaps(void);

	public:

		differencesReport getReport(void);
		void findCommonRanges(int i, int athread, int rangeStart, int rangeEnd);
		void progressToConsole(std::chrono::time_point<std::chrono::system_clock> startOperations);
		void dumpBuffersintoArray(void);
		void sortRanges(void);
		void readIntoBuffers(void);
		bool openForComparison(const char* oldFilePath, const char* newFilePath);
		void writeToPatchFile(std::fstream* afileStream);
		void displayDifferences(void);

		dashDiff();
		~dashDiff();

	};

}
