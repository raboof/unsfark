#include <stdio.h>
#include "unsfark.h"

int main(int argc, char * argv[])
{
   register char *   str;
   SFARKHANDLE      sfark;

   if (argc < 2)
      printf("Please specify the desired sfArk filename!\n");

   else if (!(sfark = SfarkAlloc()))
      printf("Not enough RAM!\n");
   else
   {
      int         errCode;

      errCode = 0;

      if (!(errCode = SfarkOpen(sfark, argv[1])))
      {
         if (argc > 2)
            str = argv[2];
         else
            str = SfarkGetBuffer(sfark);

         if (!(errCode = SfarkBeginExtract(sfark, str)))
         {
            unsigned char   dots;

            dots = 0;
            do
            {
               if (dots != SfarkPercent(sfark))
               {
                  printf("*");
                  ++dots;
               }
            } while (!(errCode = SfarkExtract(sfark)));
            if (errCode > 0) errCode = 0;
            printf("\n");
         }
      }

      str = (char *)SfarkErrMsg(sfark, errCode);
      printf("\n%s\n", str);

      SfarkFree(sfark);
   }

    return 0;
}
