#include "HdCataWmoStreaming.h"
#include <cstring>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
namespace HD::CataWmoStreaming {
namespace {
Interest interest;bool initialized=false,enabled=false;
bool Toggle(CVar*,const char*,const char*s,void*){if(!s||!(std::strcmp(s,"0")==0||std::strcmp(s,"1")==0))return false;enabled=s[0]=='1';interest.valid=false;return true;}
}
void Initialize(){if(initialized)return;auto*c=CVar::Register("hdCataWmoStreaming","HD: Cata WMO async guard + min(farclip,380.25) blocking range; 0 native",1,"0",Toggle,CATEGORY_GRAPHICS,false,nullptr,false);initialized=true;enabled=c&&c->m_intValue!=0;}
void Shutdown(){initialized=false;enabled=false;Invalidate();}
bool Enabled(){return initialized&&enabled;}
void Invalidate(){interest.valid=false;}
void Update(Point p,float farClip,float cornerDistance){interest.Update(p,farClip,cornerDistance);}
bool Prepare(Box box,bool native){return Enabled()?interest.Prepare(box,native):native;}
bool RootWait(Box box){return !Enabled()||interest.RootWait(box);}
bool GroupWait(Box box,bool nativeCollision){return Enabled()?interest.GroupWait(box,nativeCollision):nativeCollision;}
}
