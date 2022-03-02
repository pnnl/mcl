
#ifndef DEBUG_H
#define DEBUG_H

//#define VERBOSE

#if _DEBUG
#define Dprintf(fmt, args...) fprintf(stderr,"[MCL DEBUG][%s %d] " fmt "\n", __FILE__, __LINE__, ##args)
#define dprintf(fmt, args...) fprintf(stderr,"" fmt, ##args)
#else
#define Dprintf(fmt, args...)
#define dprintf(fmt, args...)
#endif

#if defined (_DEBUG) && defined (VERBOSE)
#define VDprintf(fmt, args...) fprintf(stderr,"[MCL DEBUG][%s %d] " fmt "\n", __FILE__, __LINE__, ##args)
#else
#define VDprintf(fmt, args...)
#endif

#define eprintf(fmt, args...) fprintf(stderr,"[MCL ERR][%s %d] " fmt "\n", __FILE__, __LINE__, ##args)

#define iprintf(fmt, args...) fprintf(stderr,"[MCL INFO] " fmt "\n", ##args)

#endif
