package Shared
{
   import Shared.AS3.Data.BSUIDataManager;
   import Shared.AS3.Events.CustomEvent;

   public class GlobalFunc
   {
      public static const SHOW_HUD_MESSAGE:String = "GlobalFunc::showHUDMessage";

      public static function ShowHUDMessage(param1:String) : *
      {
         BSUIDataManager.dispatchEvent(new CustomEvent(SHOW_HUD_MESSAGE,{"text":param1}));
      }
   }
}
