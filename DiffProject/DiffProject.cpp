#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <iomanip>

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
		char *start;
		char *reference;
		char *end;

		char *min;
		char *max;

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

		void add(char *reference, char *start, char *end, char *min, char *max)
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

	class dashDiff
	{	
	private:
		// File stream handles for comparison.
		std::fstream oldFile;
		std::fstream newFile;

		char * oldFileBuffer;
		char *newFileBuffer;
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

		bool valuesOverlap(characterRange& a, characterRange& b)
		{
			// if the space required to fit both ranges is less than the sum of the two ranges, they overlap.
			if ((int)std::max(a.end, b.end) - (int)std::min(a.start, b.start) < (a.end - a.start) + (b.end - b.start))
			{
				return true;
			}
			return false;
		}

		void removeWeakOverlaps(uint8_t* argOldBuff, uint8_t* argNewBuff)
		{
			// All the ranges need to be in the same order per side, as all we can do now to differentiate the files is delete from the old
			// or insert into the new. The number of bytes is immaterial, but out of order ranges are fools dreams. The decision is easy, however.
			// The bigger range wins.

			for (int i = 0; i < rangeVector.size(); i++)
			{
				for (int j = i; j < rangeVector.size(); j++)
				{
					if (i == j)
						continue;

					if ((rangeVector[i].oldRange.start > rangeVector[j].oldRange.start &&
						rangeVector[i].newRange.start < rangeVector[j].newRange.start) ||
						(rangeVector[i].oldRange.start < rangeVector[j].oldRange.start &&
							rangeVector[i].newRange.start > rangeVector[j].newRange.start))
					{
						// Delete the smaller of the two as they are out of order.
						if (rangeVector[i].rangeSize >= rangeVector[j].rangeSize)
						{
							for (int flagit = 0; flagit < rangeVector[j].oldRange.sizeofRange(); flagit++)
							{
								bufferMutex.lock();
								argOldBuff[rangeVector[j].oldRange.start - &oldFileBuffer[0] + flagit]--;
								argNewBuff[rangeVector[j].newRange.start - &newFileBuffer[0] + flagit]--;
								bufferMutex.unlock();
							}

							rangeVector.erase(rangeVector.begin() + j);
							j--;
						}
						else
						{
							for (int flagit = 0; flagit < rangeVector[i].oldRange.sizeofRange(); flagit++)
							{
								bufferMutex.lock();
								argOldBuff[rangeVector[i].oldRange.start - &oldFileBuffer[0] + flagit]--;
								argNewBuff[rangeVector[i].newRange.start - &newFileBuffer[0] + flagit]--;
								bufferMutex.unlock();
							}

							rangeVector.erase(rangeVector.begin() + i);
							i--;
							break;
						}

					}
				}
			}
		}

		void removeOverlapEntry(int *it, uint8_t* oldBufferFlag, uint8_t* newBufferFlag)
		{
			for (int flagit = 0; flagit < rangeVector[*it].oldRange.sizeofRange(); flagit++)
			{
				bufferMutex.lock();
				oldBufferFlag[rangeVector[*it].oldRange.start - &oldFileBuffer[0] + flagit]--;
				newBufferFlag[rangeVector[*it].newRange.start - &newFileBuffer[0] + flagit]--;
				bufferMutex.unlock();
			}

			rangeVector.erase(rangeVector.begin() + *it);

			*it--;
		}

		void reduceOverlaps(uint8_t* oldBufferFlag, uint8_t* newBufferFlag)
		{
			if (rangeVector.size() > 1)
			{
				for (int xt = 0; xt < rangeVector.size(); xt++)
				{
					for (int it = xt; it < rangeVector.size(); it++)
					{
						if (xt == it)
							continue;

						if (valuesOverlap(rangeVector[xt].oldRange, rangeVector[it].oldRange))
						{
							// If this range is bigger than the one we're comparing it to, remove the smaller one.

							if (rangeVector[xt].oldRange > rangeVector[it].oldRange)
							{
								removeOverlapEntry(&it, oldBufferFlag, newBufferFlag);
							}
							else
							{
								removeOverlapEntry(&xt, oldBufferFlag, newBufferFlag);
							}

						}
						else if (valuesOverlap(rangeVector[xt].newRange, rangeVector[it].newRange))
						{
							// If this range is bigger than the one we're comparing it to, remove the smaller one.

							if (rangeVector[xt].newRange > rangeVector[it].newRange)
							{
								removeOverlapEntry(&it, oldBufferFlag, newBufferFlag);
							}
							else
							{
								removeOverlapEntry(&xt, oldBufferFlag, newBufferFlag);
							}

						}

					}
				}
			}
		}

	public:

		differencesReport getReport(void)
		{
			return report;
		}

		void findCommonRanges(int i, int athread, uint8_t* oldBufferFlag, uint8_t* newBufferFlag)
		{
			std::vector<dualRange> localRangeVector;

			for (int j = 0; j < oldFileBufferArray[i].pointerBuffer.size(); j++)
			{
				for (int x = 0; x < newFileBufferArray[i].pointerBuffer.size(); x++)
				{
					char* oleft, * oright;
					char* nleft, * nright;
					int range = 1;

					threadPercent[athread] = (int)((float)(j * newFileBufferArray[i].pointerBuffer.size() + x) / (float)(oldFileBufferArray[i].pointerBuffer.size() * newFileBufferArray[i].pointerBuffer.size()) * 100);

					/*bufferMutex.lock();
					if (oldBufferFlag[oldFileBufferArray[i].pointerBuffer[j].reference - &oldFileBuffer[0]] > 0 &&
						newBufferFlag[newFileBufferArray[i].pointerBuffer[x].reference - &newFileBuffer[0]] > 0)
					{
						bufferMutex.unlock();
						continue;
					}
					bufferMutex.unlock();*/

					oleft = oright = oldFileBufferArray[i].pointerBuffer[j].reference;
					nleft = nright = newFileBufferArray[i].pointerBuffer[x].reference;
					// Expand to the left as much as we can while each character matches
					while (true)
					{
						if (oleft == oldFileBufferArray[i].pointerBuffer[j].min || nleft == newFileBufferArray[i].pointerBuffer[x].min)
							break;

						if (*(--oleft) != *(--nleft))
							break;

						range++;
					}

					// Now expand to the right as much as we can.
					while (true)
					{
						if (oright == oldFileBufferArray[i].pointerBuffer[j].max || nright == newFileBufferArray[i].pointerBuffer[x].max)
							break;

						if (*(++oright) != *(++nright))
							break;

						range++;
					}

					dualRange tempRange;
					bool itSafe = true;

					tempRange.oldRange.start = oleft++;
					tempRange.oldRange.end = oright;
					tempRange.oldRange.min = oldFileBufferArray[i].pointerBuffer[j].min;
					tempRange.oldRange.max = oldFileBufferArray[i].pointerBuffer[j].max;
					tempRange.oldRange.reference = oldFileBufferArray[i].pointerBuffer[j].reference;
					tempRange.newRange.start = nleft++;
					tempRange.newRange.end = nright;
					tempRange.newRange.min = newFileBufferArray[i].pointerBuffer[x].min;
					tempRange.newRange.max = newFileBufferArray[i].pointerBuffer[x].max;
					tempRange.newRange.reference = newFileBufferArray[i].pointerBuffer[x].reference;

					for (int rangeTest = 0; rangeTest < localRangeVector.size(); rangeTest++)
					{
						if (valuesOverlap(tempRange.oldRange, localRangeVector[rangeTest].oldRange) ||
							valuesOverlap(tempRange.newRange, localRangeVector[rangeTest].newRange))
						{
							itSafe = false;
							break;
						}
					}

					if (range > 4 && itSafe) // Set to a 5 minimum because the code for S[text] is 4 bytes long as a minimum.
					{				// So while we can skip that text, it really doesm't save us anything and just increases
									// the size of the patch file, and computation time.
						dualRange response;


						// Alright old man, you sped, and now it's time to pay the man. Let me fill you out a ticket.
						response.rangeSize = range;
						response.oldRange.end = oright;
						response.oldRange.start = oleft;
						response.newRange.end = nright;
						response.newRange.start = nleft;
						response.newRange.max = newFileBufferArray[i].pointerBuffer[x].max;
						response.newRange.min = newFileBufferArray[i].pointerBuffer[x].min;
						response.newRange.reference = newFileBufferArray[i].pointerBuffer[x].reference;

						localRangeVector.push_back(response);

						for (int bfit = 0; bfit < response.rangeSize; bfit++)
						{
							bufferMutex.lock();
							oldBufferFlag[response.oldRange.start - &oldFileBuffer[0] + bfit]++;
							newBufferFlag[response.newRange.start - &newFileBuffer[0] + bfit]++;
							bufferMutex.unlock();
						}

					}

				}
			}

			if (localRangeVector.size() > 0)
			{
				rangeVectorMutex.lock();

				rangeVector.insert(rangeVector.end(), localRangeVector.begin(), localRangeVector.end());

				removeWeakOverlaps(oldBufferFlag, newBufferFlag);
				reduceOverlaps(oldBufferFlag, newBufferFlag);

				rangeVectorMutex.unlock();
			}

			threadActive[athread] = false; // Feels wrong, but here we are.
		}

		void dumpBuffersintoArray(void)
		{
			int threadId[THREADCOUNT];
			const std::chrono::time_point<std::chrono::system_clock> startOperations = std::chrono::system_clock::now();
			uint8_t* oldBufferFlag = nullptr;
			uint8_t* newBufferFlag = nullptr;

			oldBufferFlag = new uint8_t[oldFileBufferSize];
			newBufferFlag = new uint8_t[newFileBufferSize];

			// Optimization: If a range has already been added, and our character falls INSIDE that range, then there is no way we can expand upon it.
			//			     So it won't be bigger, only the same size, and therefore there is no reason to even look. Must be true on both the old and new side.


			for (int i = 0; i < oldFileBufferSize; i++)
			{
				oldBufferFlag[i] = 0;
			}
			for (int i = 0; i < newFileBufferSize; i++)
			{
				newBufferFlag[i] = 0;
			}

			memset(threadId, 0, sizeof(threadId));

			// Dump the buffers into the arrays for sorting.
			for (int i = 0; i < oldFileBufferSize; i++)
			{	// 128 + because char's are unsigned by default. I only missed this because I was using plaintext before.
				oldFileBufferArray[128+oldFileBuffer[i]].add(&oldFileBuffer[i], &oldFileBuffer[i], &oldFileBuffer[i], &oldFileBuffer[0], &oldFileBuffer[oldFileBufferSize - 1]);
			}

			for (int i = 0; i < newFileBufferSize; i++)
			{
				newFileBufferArray[128+newFileBuffer[i]].add(&newFileBuffer[i], &newFileBuffer[i], &newFileBuffer[i], &newFileBuffer[0], &newFileBuffer[newFileBufferSize - 1]);
			}

			std::thread* workthread[THREADCOUNT];

			for (int i = 0; i < THREADCOUNT; i++)
				workthread[i] = nullptr;

			for (int i = 0; i < 256; i++)
			{
				/*char wheelbarrow[] = {'-', '\\', '|', '/'};
				uint8_t wheelbarrowPlus = 0;
				uint8_t wheelbarrowNeg = 0;
				uint8_t oldwheelbarrowPlus = 0;
				uint8_t oldwheelbarrowNeg = 0;*/

				if (oldFileBufferArray[i].pointerBuffer.size() == 0 || newFileBufferArray[i].pointerBuffer.size() == 0)
					continue; // Skip this character if it doesn't exist in both files, it can't be valid.

				// Check to see if any threads are joinable, if so make them null.
				while (true)
				{
					bool nullExists = false;
					for (int x = 0; x < THREADCOUNT; x++)
					{
						if (workthread[x] != nullptr)
						{
							if (!threadActive[x])
							{
								delete workthread[x];
								workthread[x] = nullptr;

								nullExists = true;
							}
						}
						else
							nullExists = true;
					}

					for (int x = 0; x < THREADCOUNT; x++)
					{
						if (threadActive[x])
							std::cout << "[" << std::setw(3) << threadPercent[x] << "%]";
						else
							std::cout << "[___%]";
					}

					{
						const std::chrono::time_point<std::chrono::system_clock> currentOperations = std::chrono::system_clock::now();
						std::cout << "  Elapsed Time: " << std::chrono::duration_cast<std::chrono::seconds>(currentOperations - startOperations).count();


						rangeVectorMutex.lock();
						int entriespersecond = 0;

						if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count() > 0)
							entriespersecond = rangeVector.size() / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count();
						rangeVectorMutex.unlock();

						std::cout << " second(s) {" << entriespersecond << " matches per second}        \r";
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(100));

					if (nullExists)
						break;
				}
				// Check to see if any threads are null, if so, make them work.
				for (int x = 0; x < THREADCOUNT; x++)
				{
					if (workthread[x] == nullptr)
					{
						workthread[x] = new std::thread(&dashDiff::findCommonRanges, this, i, x, oldBufferFlag, newBufferFlag);
						threadId[x] = i;
						threadActive[x] = true;
						workthread[x]->detach();

						break;
					}
				}

			}

			// Wait for all threads to finish
			while (true)
			{
				bool activethread = false;
				for (int i = 0; i < THREADCOUNT; i++)
				{
					if (threadActive[i])
					{
						activethread = true;
						break;
					}
				}
				if (!activethread)
					break;

				for (int x = 0; x < THREADCOUNT; x++)
				{
					if (threadActive[x])
						std::cout << "[" << std::setw(3) << threadPercent[x] << "%]";
					else
						std::cout << "[___%]";
				}

				{
					const std::chrono::time_point<std::chrono::system_clock> currentOperations = std::chrono::system_clock::now();
					std::cout << "  Elapsed Time: " << std::chrono::duration_cast<std::chrono::seconds>(currentOperations - startOperations).count();


					rangeVectorMutex.lock();
					int entriespersecond = 0;

					if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count() > 0)
						entriespersecond = rangeVector.size() / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count();
					rangeVectorMutex.unlock();

					std::cout << " second(s) {" << entriespersecond << " matches per second}        \r";
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			for (int x = 0; x < THREADCOUNT; x++)
			{
				if (threadActive[x])
					std::cout << "[" << std::setw(3) << threadPercent[x] << "%]";
				else
					std::cout << "[___%]";
			}

			{
				const std::chrono::time_point<std::chrono::system_clock> currentOperations = std::chrono::system_clock::now();
				std::cout << "  Elapsed Time: " << std::chrono::duration_cast<std::chrono::seconds>(currentOperations - startOperations).count();


				rangeVectorMutex.lock();
				int entriespersecond = 0;

				if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count() > 0)
					entriespersecond = rangeVector.size() / std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - startOperations).count();
				rangeVectorMutex.unlock();

				std::cout << " second(s) {" << entriespersecond << " matches per second}        \r";
			}

			delete oldBufferFlag;
			delete newBufferFlag;

		}

		void sortRanges(void)
		{
			// Sort the ranges by the start of the old range.
			std::sort(rangeVector.begin(), rangeVector.end());
		}

		void readIntoBuffers(void)
		{
			// Check if the files are open.
			if (!oldFile.is_open() || !newFile.is_open())
			{
				std::cout << "dashDiff::dashDiff.readIntoBuffers(): Files are not open for reading." << std::endl;
				exit(-1);
			}

			// Get the size of the files.
			oldFile.seekg(0, std::ios::end);
			oldFileBufferSize = oldFile.tellg();
			oldFile.seekg(0, std::ios::beg);

			newFile.seekg(0, std::ios::end);
			newFileBufferSize = newFile.tellg();
			newFile.seekg(0, std::ios::beg);

			// Allocate the buffers.
			oldFileBuffer = new char[oldFileBufferSize];
			newFileBuffer = new char[newFileBufferSize];

			// Read the files into the buffers.
			oldFile.read(oldFileBuffer, oldFileBufferSize);
			newFile.read(newFileBuffer, newFileBufferSize);

			// Captain, Captain, ready to rip.
		}

		bool openForComparison(const char* oldFilePath, const char* newFilePath)
		{
			report = { 0, 0, 0, 0, 0 };

			// open both files as binary input streams.
			oldFile.open(oldFilePath, std::ios::in | std::ios::binary);
			newFile.open(newFilePath, std::ios::in | std::ios::binary);

			// If either file failed to open, return false.
			if (!oldFile.is_open() || !newFile.is_open())
			{
				return false;
			}

			return true;
		}

		void writeToPatchFile(std::fstream *afileStream)
		{
			char *oldFilePointer = oldFileBuffer;
			char *newFilePointer = newFileBuffer;

			report.oldFileSize = oldFileBufferSize;
			report.newFileSize = newFileBufferSize;

			for (int i = 0; i < rangeVector.size(); i++)
			{
				// Delete everything in the old file before the range.
				if (oldFilePointer != rangeVector[i].oldRange.start)
				{
					*afileStream << "-[" << rangeVector[i].oldRange.start - oldFilePointer << "]";
					report.deletedCharacters += rangeVector[i].oldRange.start - oldFilePointer;
					oldFilePointer = rangeVector[i].oldRange.start;
				}
				// Add everything in the new file before the range.
				if (newFilePointer != rangeVector[i].newRange.start)
				{
					*afileStream << "+[" << rangeVector[i].newRange.start - newFilePointer << "]";
					while (newFilePointer < rangeVector[i].newRange.start)
					{
						*afileStream << *newFilePointer;
						newFilePointer++;
						report.insertedCharacters++;
					};
				}
				// Skip the range
				*afileStream << "S[" << rangeVector[i].oldRange.end - rangeVector[i].oldRange.start << "]";
				oldFilePointer = rangeVector[i].oldRange.end;
				newFilePointer = rangeVector[i].newRange.end;
				report.sameCharacters += rangeVector[i].oldRange.end - rangeVector[i].oldRange.start;
			}
			// Print the rest of the old file.
			if (oldFilePointer != &oldFileBuffer[oldFileBufferSize])
			{
				*afileStream << "-[" << &oldFileBuffer[oldFileBufferSize] - oldFilePointer << "]";
				report.deletedCharacters += &oldFileBuffer[oldFileBufferSize] - oldFilePointer;
			}
			// Print the rest of the new file.
			if (newFilePointer != &newFileBuffer[newFileBufferSize])
			{
				*afileStream << "+[" << &newFileBuffer[newFileBufferSize] - newFilePointer << "]";
				while (newFilePointer != &newFileBuffer[newFileBufferSize])
				{
					*afileStream << *newFilePointer;
					newFilePointer++;
					report.insertedCharacters++;
				}
			}
		}

		void displayDifferences(void)
		{
			char *oldFilePointer = oldFileBuffer;
			char *newFilePointer = newFileBuffer;

			for (int i = 0; i < rangeVector.size(); i++)
			{
				std::cout << "Range :" << i << "[";
				oldFilePointer = rangeVector[i].oldRange.start;
				while (oldFilePointer != rangeVector[i].oldRange.end)
				{
					std::cout << *oldFilePointer;
					oldFilePointer++;
				}
				std::cout << "][";
				newFilePointer = rangeVector[i].newRange.start;
				while (newFilePointer != rangeVector[i].newRange.end)
				{
					std::cout << *newFilePointer;
					newFilePointer++;
				}
				std::cout << "]" << std::endl;
			}

			oldFilePointer = oldFileBuffer;
			newFilePointer = newFileBuffer;

			// Iterate through the old file's vector
			for (int i = 0; i < rangeVector.size(); i++)
			{
				// Delete everything in the old file before the range.
				if (oldFilePointer != rangeVector[i].oldRange.start)
				{
					std::cout << "-[" << rangeVector[i].oldRange.start - oldFilePointer << "]";
					oldFilePointer = rangeVector[i].oldRange.start;
				}
				// Add everything in the new file before the range.
				if (newFilePointer != rangeVector[i].newRange.start)
				{
					std::cout << "+[";
					do 
					{
						std::cout << *newFilePointer;
						newFilePointer++;
					} while (newFilePointer < rangeVector[i].newRange.start);
					std::cout << "]";
				}
				// Print the range.
				std::cout << "S[" << rangeVector[i].oldRange.end - rangeVector[i].oldRange.start << "]";
				do 
				{
					//std::cout << *oldFilePointer;
					oldFilePointer++;
					newFilePointer++;
				} while (oldFilePointer < rangeVector[i].oldRange.end);
			}
			// Print the rest of the old file.
			if (oldFilePointer != &oldFileBuffer[oldFileBufferSize])
			{
				std::cout << "-[" << &oldFileBuffer[oldFileBufferSize] - oldFilePointer << "]";
			}
			// Print the rest of the new file.
			std::cout << "+[";
			while (newFilePointer != &newFileBuffer[newFileBufferSize])
			{
				std::cout << *newFilePointer;
				newFilePointer++;
			}
			std::cout << "]";

		}

		dashDiff()
		{
			oldFileBuffer = nullptr;
			newFileBuffer = nullptr;
			oldFileBufferSize = newFileBufferSize = 0;

			for (int i = 0; i < THREADCOUNT; i++)
			{
				threadActive[i] = false;
				threadPercent[i] = 0;
			}
		}
		~dashDiff()
		{
			// If the files are open, close them.
			if (oldFile.is_open())
			{
				oldFile.close();
			}
			if (newFile.is_open())
			{
				newFile.close();
			}

			free( oldFileBuffer);
			free( newFileBuffer);
		}
	};	

}
int main(int argc, char** argv)
{
	std::vector<std::string> FileList;
	std::string patchFile = "patch.dph";
	std::fstream patchFileStream;

	dashDiff::dashDiff dashDiff;

	std::cout << "dashDiff v0.5 (c) 2023 Christopher Laverdure" << std::endl;
	std::cout << "All rights reserved. If it went kapoop, I didn't do it. That code was written by a guy named Bob." << std::endl;
	std::cout << "We must all try to hurt Bob whenever he exposes himself from between the cushions of the code." << std::endl;
	std::cout << "=-----------------------------------------------------------------------------------------------=" << std::endl;
	
	// Let's look for our arguments.
	for (int i = 1; i < argc; i++)
	{
		FileList.push_back(argv[i]);
	}

	FileList.push_back("prboomp_enemy.c");
	FileList.push_back("chocolatedoomp_enemy.c");

	// We're only going to do two files at a time for now.
	if (FileList.size() != 2)
	{
		std::cout << "dashDiff::main(): Invalid number of files specified." << std::endl;
		//return -1;
	}

	if (!dashDiff.openForComparison(FileList[0].c_str(), FileList[1].c_str()))
	{
		std::cout << "Failed to open files for comparison." << std::endl;
		return -1;
	}

	// Open patch file for writing, overwrite any data there if it exists.
	patchFileStream.open(patchFile, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!patchFileStream.is_open())
	{
		std::cout << "dashDiff::main(): Failed to open patch file for writing." << std::endl;
		return -1;
	}

	std::cout << "Processing differences between " << FileList[0] << " and " << FileList[1] << std::endl;

	// The first line in the file is going to be the old file name.
	patchFileStream << FileList[0] << std::endl;
	// followed by the new file name.
	patchFileStream << FileList[1] << std::endl;

	dashDiff.readIntoBuffers();
	dashDiff.dumpBuffersintoArray();
	dashDiff.sortRanges();
    dashDiff.writeToPatchFile(&patchFileStream);

	// Close the patch file.
	patchFileStream.close();

	dashDiff::differencesReport report = dashDiff.getReport();

	std::cout << std::endl << "Differences Report:" << std::endl;
	std::cout << "Old File Size: " << report.oldFileSize << std::endl;
	std::cout << "New File Size: " << report.newFileSize << std::endl;
	std::cout << "Characters Deleted: " << report.deletedCharacters << std::endl;
	std::cout << "Characters Deleted (Percentage of old Document):" << (float)report.deletedCharacters / (float)report.oldFileSize * 100.0f << "%" << std::endl;
	std::cout << "Characters Inserted: " << report.insertedCharacters << std::endl;
	std::cout << "Characters Inserted (Percentage of new Document):" << (float)report.insertedCharacters / (float)report.newFileSize * 100.0f << "%" << std::endl;
	std::cout << "Characters Same: " << report.sameCharacters << std::endl;
	std::cout << "Characters Same (Percentage of old Document):" << (float)report.sameCharacters / (float)report.oldFileSize * 100.0f << "%" << std::endl;

	return 0;
}