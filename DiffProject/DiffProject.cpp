#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <thread>

#include <stdio.h>

namespace dashDiff
{   
	// Stores all the information about our character matching for finding similar blocks of text.
	class characterRange
	{
	public:
		char *start;
		char *reference;
		char *end;

		char *min;
		char *max;

		uint32_t sizeofRange(void)
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
		uint32_t rangeSize;

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

		char *oldFileBuffer;
		char *newFileBuffer;
		uint32_t oldFileBufferSize;
		uint32_t newFileBufferSize;

		fileByteBuffer oldFileBufferArray[256];
		fileByteBuffer newFileBufferArray[256];

		std::vector<dualRange> rangeVector;

		bool valuesOverlap(characterRange& a, characterRange& b)
		{
			// if the space required to fit both ranges is less than the sum of the two ranges, they overlap.
			if ((int)std::max(a.end, b.end) - (int)std::min(a.start, b.start) < (a.end - a.start) + (b.end - b.start))
			{
				return true;
			}
			return false;
		}

	public:

		void dumpBuffersintoArray(void)
		{

			// Dump the buffers into the arrays for sorting.
			for (int i = 0; i < oldFileBufferSize; i++)
			{
				oldFileBufferArray[oldFileBuffer[i]].add(&oldFileBuffer[i], &oldFileBuffer[i], &oldFileBuffer[i], &oldFileBuffer[0], &oldFileBuffer[oldFileBufferSize - 1]);
			}

			for (int i = 0; i < newFileBufferSize; i++)
			{
				newFileBufferArray[newFileBuffer[i]].add(&newFileBuffer[i], &newFileBuffer[i], &newFileBuffer[i], &newFileBuffer[0], &newFileBuffer[newFileBufferSize - 1]);
			}

			for (int i = 0; i < 256; i++)
			{
				if (oldFileBufferArray[i].pointerBuffer.size() == 0 || newFileBufferArray[i].pointerBuffer.size() == 0)
					continue; // Skip this character if it doesn't exist in both files, it can't be matched.

				for (int j = 0; j < oldFileBufferArray[i].pointerBuffer.size(); j++)
				{
					char wheelbarrow[] = { '-', '\\', '|', '/' };
					int wheelbarrowPlus = 0;
					int wheelbarrowNeg = 0;

					for (int x = 0; x < newFileBufferArray[i].pointerBuffer.size(); x++)
					{
						char* oleft, * oright;
						char* nleft, * nright;
						int range = 1;

						// Only do this every 15 passes.
						if (x % 50 == 0)
							std::cout << "\r[" << i << "]" << "{" << j << "/" << oldFileBufferArray[i].pointerBuffer.size() << " -> " << rangeVector.size() << "} ";

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

						// Check the range to see if this overlaps with anything already in there.
						dualRange tempRange;
						
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

						if (rangeVector.size() > 1)
						{
							for (auto it = rangeVector.begin(); it != rangeVector.end(); it++)
							{
								if (valuesOverlap(tempRange.oldRange, it->oldRange))
								{
									// If this range is bigger than the one we're comparing it to, remove the smaller one.

									if (tempRange.oldRange > it->oldRange)
									{
										wheelbarrowNeg++;
										it = rangeVector.erase(it);
										if (it != rangeVector.begin())
											it--;

										if (rangeVector.empty())
											break;
									}
									else
									{
										range = 0;
										break;
									}

								}
								else if (valuesOverlap(tempRange.newRange, it->newRange))
								{
									// If this range is bigger than the one we're comparing it to, remove the smaller one.

									if (tempRange.newRange > it->newRange)
									{
										wheelbarrowNeg++;
										it = rangeVector.erase(it);

										if (it != rangeVector.begin())
											it--;

										if (rangeVector.empty())
											break;
									}
									else
									{
										range = 0;
										break;
									}

								}

							}
						}

						if (range > 1) // It only really counts if it's bigger than 1.
						{
							dualRange response;


							// Alright old man, you sped, and now it's time to pay the man. Let me fill you out a ticket.
							response.rangeSize = range;
							response.oldRange.end = oright;
							response.oldRange.start = oleft;
							response.oldRange.max = oldFileBufferArray[i].pointerBuffer[j].max;
							response.oldRange.min = oldFileBufferArray[i].pointerBuffer[j].min;
							response.oldRange.reference = oldFileBufferArray[i].pointerBuffer[j].reference;
							response.newRange.end = nright;
							response.newRange.start = nleft;
							response.newRange.max = newFileBufferArray[i].pointerBuffer[x].max;
							response.newRange.min = newFileBufferArray[i].pointerBuffer[x].min;
							response.newRange.reference = newFileBufferArray[i].pointerBuffer[x].reference;

							// Display the contents of both ranges
							/*std::cout << "[";
							while (oleft != oright)
							{
								std::cout << *oleft;
								oleft++;
							}
							std::cout << "]->[";
							while (nleft != nright)
							{
								std::cout << *nleft;
								nleft++;
							}
							std::cout << "]" << std::endl;*/

							rangeVector.push_back(response);
							wheelbarrowPlus++;

							if (x % 50 == 0)
								std::cout << "In: [" << wheelbarrow[wheelbarrowPlus % 4] << "] Out: [" << wheelbarrow[wheelbarrowNeg % 4] << "]                     \r";
						}
						
					}
				}
			}
		}

		void removeWeakOverlaps(void)
		{
			std::cout << std::endl << "=-----------------------------------------------------------------------------------------=" << std::endl;
			std::cout << "Ranges Found: " << rangeVector.size() << std::endl;

			// Check the ranges for any overlaps, if any of them overlap the bigger one wins.

			/*for (int i = 0; i < rangeVector.size(); i++)
			{
				for (int j = 0; j < rangeVector.size(); j++)
				{
					if (i == j)
						continue;

					if (valuesOverlap(rangeVector[i].oldRange, rangeVector[j].oldRange))
					{

						if (rangeVector[i].oldRange > rangeVector[j].oldRange)
						{
							rangeVector.erase(rangeVector.begin() + j);
							j--;
						}
						else
						{
							rangeVector.erase(rangeVector.begin() + i);
							i--;
							break;
						}
					}
				}
			}

			// Check for overlaps on the new side.
			for (int i = 0; i < rangeVector.size(); i++)
			{
				for (int j = 0; j < rangeVector.size(); j++)
				{
					if (i == j)
						continue;

					if (valuesOverlap(rangeVector[i].newRange, rangeVector[j].newRange))
					{
						if (rangeVector[i].newRange > rangeVector[j].newRange)
						{
							rangeVector.erase(rangeVector.begin() + j);
							j--;
						}
						else
						{
							rangeVector.erase(rangeVector.begin() + i);
							i--;
							break;
						}
					}
				}
			}*/

			// All the ranges need to be in the same order per side, as all we can do now to differentiate the files is delete from the old
			// or insert into the new. The number of bytes is immaterial, but out of order ranges are fools dreams. The decision is easy, however.
			// The bigger range wins.

			for (int i = 0; i < rangeVector.size(); i++)
			{
				for (int j = 0; j < rangeVector.size(); j++)
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
							rangeVector.erase(rangeVector.begin() + j);
							j--;
						}
						else
						{
							rangeVector.erase(rangeVector.begin() + i);
							i--;
							break;
						}
						
					}
				}
			}

			std::cout << "Ranges After socking out of order ranges: " << rangeVector.size() << std::endl;

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

			delete oldFileBuffer;
			delete newFileBuffer;
		}
	};	

}
int main()
{
	dashDiff::dashDiff dashDiff;

	if (!dashDiff.openForComparison("prboomp_enemy.c","chocolatedoomp_enemy.c"))
	{
		std::cout << "Failed to open files for comparison." << std::endl;
		return -1;
	}

	dashDiff.readIntoBuffers();
	dashDiff.dumpBuffersintoArray();
	dashDiff.removeWeakOverlaps();
	dashDiff.displayDifferences();
	
	return 0;
}