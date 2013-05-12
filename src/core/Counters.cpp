#include "anki/core/Counters.h"
#include "anki/core/Timestamp.h"
#include "anki/util/Filesystem.h"
#include "anki/util/Array.h"
#include <cstring>

namespace anki {

#if ANKI_ENABLE_COUNTERS

//==============================================================================

enum CounterFlag
{
	CF_PER_FRAME = 1 << 0,
	CF_PER_RUN = 1 << 1,
	CF_FPS = 1 << 2, ///< Only with CF_PER_RUN
	CF_F64 = 1 << 3,
	CF_U64 = 1 << 4
};

struct CounterInfo
{
	const char* name;
	U32 flags;
};

static const Array<CounterInfo, C_COUNT> cinfo = {{
	{"FPS", CF_PER_RUN | CF_FPS | CF_F64},
	{"MAIN_RENDERER_TIME", CF_PER_FRAME | CF_PER_RUN | CF_F64},
	{"RENDERER_MS_TIME", CF_PER_FRAME | CF_PER_RUN | CF_F64},
	{"RENDERER_IS_TIME", CF_PER_FRAME | CF_PER_RUN | CF_F64},
	{"RENDERER_PPS_TIME", CF_PER_FRAME | CF_PER_RUN | CF_F64},
	{"RENDERER_DRAWCALLS_COUNT", CF_PER_RUN | CF_U64},
	{"RENDERER_VERTICES_COUNT", CF_PER_RUN | CF_U64},
	{"SCENE_UPDATE_TIME", CF_PER_RUN | CF_F64},
	{"SWAP_BUFFERS_TIME", CF_PER_RUN | CF_F64}
}};

//==============================================================================
CountersManager::CountersManager()
{
	perframeValues.resize(C_COUNT, 0);
	perrunValues.resize(C_COUNT, 0);
	counterTimes.resize(C_COUNT, 0.0);

	// Open and write the headers to the files
	perframeFile.open("./perframe_counters.csv", File::OF_WRITE);

	perframeFile.writeString("FRAME");

	for(const CounterInfo& inf : cinfo)
	{
		if(inf.flags & CF_PER_FRAME)
		{
			perframeFile.writeString(", %s", inf.name);
		}
	}

	// Open and write the headers to the other file
	perrunFile.open("./perrun_counters.csv", File::OF_WRITE);

	U i = 0;
	for(const CounterInfo& inf : cinfo)
	{
		if(inf.flags & CF_PER_RUN)
		{
			if(i != 0)
			{
				perrunFile.writeString(", %s", inf.name);
			}
			else
			{
				perrunFile.writeString("%s", inf.name);
			}

			++i;
		}
	}
}

//==============================================================================
CountersManager::~CountersManager()
{}

//==============================================================================
void CountersManager::increaseCounter(Counter counter, U64 val)
{
	ANKI_ASSERT(cinfo[counter].flags & CF_U64);

	if(cinfo[counter].flags & CF_PER_FRAME)
	{
		perframeValues[counter] += val;
	}

	if(cinfo[counter].flags & CF_PER_RUN)
	{
		perrunValues[counter] += val;
	}
}

//==============================================================================
void CountersManager::increaseCounter(Counter counter, F64 val)
{
	ANKI_ASSERT(cinfo[counter].flags & CF_F64);
	F64 f;
	memcpy(&f, &val, sizeof(F64));

	if(cinfo[counter].flags & CF_PER_FRAME)
	{
		*(F64*)(&perframeValues[counter]) += f;
	}

	if(cinfo[counter].flags & CF_PER_RUN)
	{
		*(F64*)(&perrunValues[counter]) += f;
	}
}

//==============================================================================
void CountersManager::startTimer(Counter counter)
{
	// The counter should be F64
	ANKI_ASSERT(cinfo[counter].flags & CF_F64);
	// The timer should have beeb reseted
	ANKI_ASSERT(counterTimes[counter] == 0.0);

	counterTimes[counter] = HighRezTimer::getCurrentTime();
}

//==============================================================================
void CountersManager::stopTimerIncreaseCounter(Counter counter)
{
	// The counter should be F64
	ANKI_ASSERT(cinfo[counter].flags & CF_F64);
	// The timer should have started
	ANKI_ASSERT(counterTimes[counter] > 0.0);

	F32 prevTime = counterTimes[counter];
	counterTimes[counter] = 0.0;

	increaseCounter(counter, HighRezTimer::getCurrentTime() - prevTime);
}

//==============================================================================
void CountersManager::resolveFrame()
{
	// Write new line and frame no
	perframeFile.writeString("\n%llu", getGlobTimestamp());

	U i = 0;
	for(const CounterInfo& inf : cinfo)
	{
		if(inf.flags & CF_PER_FRAME)
		{
			if(inf.flags & CF_U64)
			{
				perframeFile.writeString(", %llu", perframeValues[i]);
			}
			else if(inf.flags & CF_F64)
			{
				perframeFile.writeString(", %f", *((F64*)&perframeValues[i]));
			}
			else
			{
				ANKI_ASSERT(0);
			}

			perframeValues[i] = 0;
		}

		i++;
	}
}

//==============================================================================
void CountersManager::flush()
{
	// Resolve per run counters
	perrunFile.writeString("\n");
	U i = 0;
	U j = 0;
	for(const CounterInfo& inf : cinfo)
	{
		if(inf.flags & CF_PER_RUN)
		{
			if(j != 0)
			{
				perrunFile.writeString(", ");
			}

			if(inf.flags & CF_U64)
			{
				perrunFile.writeString("%llu", perrunValues[i]);
			}
			else if(inf.flags & CF_F64)
			{
				if(inf.flags & CF_FPS)
				{
					perrunFile.writeString("%f", 
						(F64)getGlobTimestamp() / *((F64*)&perrunValues[i]));
				}
				else
				{
					perrunFile.writeString("%f", *((F64*)&perrunValues[i]));
				}
			}
			else
			{
				ANKI_ASSERT(0);
			}

			perrunValues[i] = 0;
			++j;
		}

		++i;
	}

	// Close and flush files
	perframeFile.close();
	perrunFile.close();
}

#endif // ANKI_ENABLE_COUNTERS

} // end namespace anki
