package
{
   import flash.display.Bitmap;
   import flash.display.BitmapData;
   import flash.display.DisplayObject;
   import flash.display.DisplayObjectContainer;
   import flash.display.Graphics;
   import flash.display.Loader;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.events.IOErrorEvent;
   import flash.events.SecurityErrorEvent;
   import flash.events.TimerEvent;
   import flash.filters.DropShadowFilter;
   import flash.geom.Point;
   import Shared.AS3.Data.BSUIDataManager;
   import Shared.AS3.Data.FromClientDataEvent;
   import flash.net.SharedObject;
   import flash.net.URLLoader;
   import flash.net.URLRequest;
   import flash.system.ApplicationDomain;
   import flash.text.Font;
   import flash.text.TextField;
   import flash.text.TextFormat;
   import flash.utils.ByteArray;
   import flash.utils.Timer;
   import flash.utils.getTimer;
   import Shared.HUDModes;
   import SharedHUDTools;

   public class HudRelay extends Sprite
   {
      private static const STORE_NAME:String = "r76radio";
      private static const STORAGE_NAME:String = "r76radio.command";
      private static const DIAG_STORAGE_NAME:String = "r76radio.diag";
      private static const RELAY_STORAGE_NAME:String = "r76radio.relay";
      private static const THEME_STORAGE_NAME:String = "r76radio.theme";
      private static const POLL_MS:int = 200;
      private static const RELAY_BUILD:String = "0.3.0";
      private static const STATE_POLL_TICKS:int = 10;
      private static const MINI_WIDTH:Number = 340;
      private static const MINI_HEIGHT:Number = 92;
      private static const MINI_ART_SIZE:Number = 68;
      private static const MINI_RECORD_X:Number = 31;
      private static const MINI_RECORD_Y:Number = 37;
      private static const MINI_RECORD_RADIUS:Number = 27;
      private static const MINI_TEXT_X:Number = 92;
      private static const MINI_TEXT_WIDTH:Number = 236;
      private static const MINI_HOLD_MS:Number = 7000;
      private static const FALLBACK_HUD_X:Number = 4;
      private static const FALLBACK_HUD_Y:Number = 72;
      private static const DEFAULT_THEME_COLOR:uint = 0xB98CFF;
      private static const LAYOUT_STORAGE_NAME:String = "r76radio.layout";
      private static const KEY_SHIFT:uint = 16;
      private static const KEY_CTRL:uint = 17;
      private static const KEY_L:uint = 76;
      private static const INPUT_POLL_TICKS:uint = 3;
      private static const MOUSE_SENSITIVITY:Number = 1;
      private static const PAD_CHORD:uint = 33536;
      private static const PAD_INDEX:uint = 0;
      private static const PAD_DPAD:uint = 15;
      private static const PAD_DEADZONE:Number = 8000;
      private static const PAD_SPEED:Number = 700;
      private static const HOTKEY_COMMANDS:Array = ["toggle","next","previous",""];
      private static const EDIT_HINT:String = "DRAG TO MOVE    RIGHT CLICK TO RESET\nWHEEL: SIZE    SHIFT + WHEEL: OPACITY\nCTRL + SHIFT + L TO SAVE";
      private static const EDIT_HINT_SCALE:Number = 1.4;
      private static const LOOK_STEP:Number = 5;
      private static const LOOK_PAD_RATE:Number = 40;

      private static const STATE_PATHS:Array = [
         "../modsdata/76radio/r76radio.state.json",
         "../Data/modsdata/76radio/r76radio.state.json",
         "../../Data/modsdata/76radio/r76radio.state.json",
         "modsdata/76radio/r76radio.state.json"
      ];

      private static const ART_PATHS:Array = [
         "../modsdata/76radio/r76radio.art.pixels",
         "../Data/modsdata/76radio/r76radio.art.pixels",
         "../../Data/modsdata/76radio/r76radio.art.pixels",
         "modsdata/76radio/r76radio.art.pixels"
      ];

      private static const CJK_FONT_PATHS:Array = [
         "fonts_ja.swf",
         "../interface/fonts_ja.swf",
         "interface/fonts_ja.swf",
         "../Data/interface/fonts_ja.swf"
      ];

      private var m_Timer:Timer;
      private var m_Store:SharedObject;
      private var m_Storage:Object;
      private var m_StorageReady:Boolean = false;
      private var m_HudTools:SharedHUDTools;
      private var m_LastToken:String = "";
      private var m_LastStamp:Number = -1;
      private var m_RelayPublished:Boolean = false;
      private var m_RelayMarked:Boolean = false;
      private var m_PresenceMarked:Boolean = false;
      private var m_Baselined:Boolean = false;

      private var m_StateTicks:int = 0;
      private var m_StateLoader:URLLoader;
      private var m_StateLoading:Boolean = false;
      private var m_StateAttempt:uint = 0;
      private var m_StateRequestedAt:Number = 0;

      private var m_ToastBaselined:Boolean = false;
      private var m_SeenSignature:String = "";
      private var m_AnnouncedSignature:String = "";

      private var m_MiniPlayer:Sprite;
      private var m_MiniArtHolder:Sprite;
      private var m_MiniArt:DisplayObject;
      private var m_ArtLoader:URLLoader;
      private var m_ArtLoading:Boolean = false;
      private var m_ArtAttempt:uint = 0;
      private var m_ArtRetryTicks:uint = 0;
      private var m_ArtSignature:String = "";

      private var m_MiniTextClip:Sprite;
      private var m_MiniMask:Sprite;
      private var m_MiniTitleMask:Sprite;
      private var m_MiniLabel:TextField;
      private var m_MiniTitle:TextField;
      private var m_MiniArtist:TextField;
      private var m_MiniTime:TextField;

      private var m_MiniLive:Boolean = false;
      private var m_MiniStation:String = "";
      private var m_LiveDisc:Sprite;
      private var m_LiveDiscOverlay:Sprite;
      private var m_LiveDiscTheme:uint = 0xFFFFFFFF;

      private var m_MiniProgressBg:Sprite;
      private var m_MiniProgressFill:Sprite;
      private var m_MiniProgressHead:Sprite;
      private var m_MiniHideAt:Number = 0;
      private var m_KeepNowPlaying:Boolean = false;
      private var m_CardShouldShow:Boolean = false;
      private var m_NotificationActive:Boolean = false;

      private var m_CardFadeTimer:Timer;
      private var m_NotifyScanTicks:uint = 0;
      private var m_HudRoot:DisplayObject;
      private var m_MessagesObject:Object;

      private var m_Code:Object;
      private var m_InputReady:Boolean = false;
      private var m_InputOk:Boolean = false;
      private var m_InputTicks:uint = 0;
      private var m_InputBackoff:uint = 0;
      private var m_ChordWasDown:Boolean = false;
      private var m_EditMode:Boolean = false;
      private var m_EditFrame:Sprite;
      private var m_EditHint:TextField;
      private var m_BaseX:Number = 0;
      private var m_BaseY:Number = 0;
      private var m_OffsetX:Number = 0;
      private var m_OffsetY:Number = 0;
      private var m_LayoutLoaded:Boolean = false;
      private var m_CardScale:Number = 100;
      private var m_IniScale:Number = 100;
      private var m_IniOpacity:Number = 100;
      private var m_ScaleOverride:Number = 0;
      private var m_OpacityOverride:Number = 0;
      private var m_CardOpacity:Number = 100;
      private var m_HideInVats:Boolean = false;
      private var m_CardHidden:Boolean = false;
      private var m_HudMode:String = "";
      private var m_HotkeySpec:String = "";
      private var m_Hotkeys:Array = [];
      private var m_HotkeyWasDown:Array = [];
      private var m_KeyCache:Object = {};
      private var m_RegisteredKeys:Object = {};
      private var m_PadRegistered:Boolean = false;
      private var m_PadChordWasDown:Boolean = false;
      private var m_PadLastAt:Number = 0;

      private var m_ThemeColor:uint = DEFAULT_THEME_COLOR;
      private var m_PipboyTheme:String = "";
      private var m_ThemeScanTicks:uint = 0;
      private var m_TunedAway:Boolean = false;

      private var m_MiniState:String = "";
      private var m_MiniPositionMs:Number = 0;
      private var m_MiniDurationMs:Number = 0;
      private var m_MiniCapturedMs:Number = 0;

      private var m_MiniMarqueeTick:uint = 0;
      private var m_MiniMarqueeHold:uint = 0;

      private var m_CjkFontLoader:Loader;
      private var m_CjkFontLoading:Boolean = false;
      private var m_CjkFontReady:Boolean = false;
      private var m_CjkFontAttempt:uint = 0;
      private var m_CjkFontWait:uint = 0;
      private var m_CjkFontName:String = "";

      public function HudRelay()
      {
         super();
         try
         {
            this.m_HudTools = new SharedHUDTools("R76Radio_Hud", HUDModes.ALL);
            this.m_HudTools.Register(this.onReceiveMessage);
         }
         catch(e:Error) {}

         this.createMiniPlayer();
         try { BSUIDataManager.Subscribe("HUDColors",this.onHudColors); } catch(colorsError:Error) {}
         try { BSUIDataManager.Subscribe("HUDModeData",this.onHudMode); } catch(modeError:Error) {}

         this.m_Timer = new Timer(POLL_MS);
         this.m_Timer.addEventListener(TimerEvent.TIMER,this.onTick);
         this.m_Timer.start();

         this.m_CardFadeTimer = new Timer(30);
         this.m_CardFadeTimer.addEventListener(TimerEvent.TIMER,this.onCardFadeTick);
         this.m_CardFadeTimer.start();
      }

      private function onTick(param1:TimerEvent) : void
      {
         this.markPresence();
         this.ensureStorage();
         this.loadLayout();
         this.publishRelayStatus();
         this.updateCjkFont();

         if(this.m_StorageReady && this.m_Storage != null)
         {
            this.pollSharedCommands();
            this.m_ThemeScanTicks++;
            if(this.m_ThemeScanTicks % 10 == 0) this.pollPipboyTheme();
         }

         this.pollNowPlaying();
         this.updateNotificationState();
         this.updateMiniPlayer();
      }

      private function createMiniPlayer() : void
      {
         this.m_MiniPlayer = new Sprite();
         this.m_MiniPlayer.mouseEnabled = false;
         this.m_MiniPlayer.visible = false;
         this.addChild(this.m_MiniPlayer);

         this.m_MiniArtHolder = new Sprite();
         this.m_MiniArtHolder.x = 12;
         this.m_MiniArtHolder.y = 12;
         this.m_MiniArtHolder.mouseEnabled = false;
         this.m_MiniPlayer.addChild(this.m_MiniArtHolder);

         this.m_MiniTextClip = new Sprite();
         this.m_MiniTextClip.mouseEnabled = false;
         this.m_MiniPlayer.addChild(this.m_MiniTextClip);

         this.m_MiniLabel = this.createMiniField(10, 12, 12);
         this.m_MiniTitle = this.createMiniField(16, 26, 22);
         this.m_MiniArtist = this.createMiniField(12, 50, 17);
         this.m_MiniTime = this.createMiniField(9, 12, 12);

         this.m_MiniLabel.text = "NOW PLAYING";
         this.m_MiniTitle.text = "";
         this.m_MiniArtist.text = "";
         this.m_MiniTime.text = "";

         this.m_MiniTextClip.addChild(this.m_MiniLabel);
         this.m_MiniTextClip.addChild(this.m_MiniTitle);
         this.m_MiniTextClip.addChild(this.m_MiniArtist);
         this.m_MiniTextClip.addChild(this.m_MiniTime);

         this.m_MiniMask = new Sprite();
         this.m_MiniMask.mouseEnabled = false;
         this.m_MiniMask.graphics.beginFill(0x000000,1);
         this.m_MiniMask.graphics.drawRect(4, 0, 332, 80);
         this.m_MiniMask.graphics.endFill();
         this.m_MiniPlayer.addChild(this.m_MiniMask);
         this.m_MiniTextClip.mask = this.m_MiniMask;

         this.m_MiniTitleMask = new Sprite();
         this.m_MiniTitleMask.mouseEnabled = false;
         this.m_MiniTitleMask.graphics.beginFill(0x000000,1);
         this.m_MiniTitleMask.graphics.drawRect(MINI_TEXT_X, 26, MINI_TEXT_WIDTH, 22);
         this.m_MiniTitleMask.graphics.endFill();
         this.m_MiniPlayer.addChild(this.m_MiniTitleMask);
         this.m_MiniTitle.mask = this.m_MiniTitleMask;

         this.m_MiniProgressBg = new Sprite();
         this.m_MiniProgressBg.x = MINI_TEXT_X;
         this.m_MiniProgressBg.y = 80;
         this.m_MiniProgressBg.mouseEnabled = false;
         this.m_MiniPlayer.addChild(this.m_MiniProgressBg);

         this.m_MiniProgressFill = new Sprite();
         this.m_MiniProgressFill.x = MINI_TEXT_X;
         this.m_MiniProgressFill.y = 80;
         this.m_MiniProgressFill.mouseEnabled = false;
         this.m_MiniProgressFill.scaleX = 0;
         this.m_MiniPlayer.addChild(this.m_MiniProgressFill);

         this.m_MiniProgressHead = new Sprite();
         this.m_MiniProgressHead.x = MINI_TEXT_X;
         this.m_MiniProgressHead.y = 80;
         this.m_MiniProgressHead.mouseEnabled = false;
         this.m_MiniProgressHead.visible = false;
         this.m_MiniPlayer.addChild(this.m_MiniProgressHead);

         this.applyCardTheme();
      }

      private function drawMiniPlayerChrome() : void
      {
         if(this.m_MiniPlayer == null) return;

         var background:Graphics = this.m_MiniPlayer.graphics;
         background.clear();

         background.beginFill(this.shadeColor(this.m_ThemeColor, 0.05), 0.9);
         background.moveTo(8, 0);
         background.lineTo(MINI_WIDTH, 0);
         background.lineTo(MINI_WIDTH, MINI_HEIGHT - 8);
         background.lineTo(MINI_WIDTH - 8, MINI_HEIGHT);
         background.lineTo(0, MINI_HEIGHT);
         background.lineTo(0, 8);
         background.lineTo(8, 0);
         background.endFill();

         background.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.8), 1);
         background.moveTo(8, 0);
         background.lineTo(MINI_WIDTH, 0);
         background.lineTo(MINI_WIDTH, MINI_HEIGHT - 8);
         background.lineTo(MINI_WIDTH - 8, MINI_HEIGHT);
         background.lineTo(0, MINI_HEIGHT);
         background.lineTo(0, 8);
         background.lineTo(8, 0);

         this.drawMiniArtPlaceholder();
         this.drawProgressBar();
      }

      private function drawProgressBar() : void
      {
         if(this.m_MiniProgressBg == null) return;

         var progressBg:Graphics = this.m_MiniProgressBg.graphics;
         progressBg.clear();

         var segments:int = 24;
         var segmentWidth:Number = 6;
         var segmentGap:Number = (MINI_TEXT_WIDTH - segments * segmentWidth) / (segments - 1);

         if(this.m_MiniLive)
         {
            progressBg.beginFill(this.shadeColor(this.m_ThemeColor, 0.3), 1);
            var seg:int = 0;
            while(seg < segments)
            {
               progressBg.drawRect(Math.round(seg * (segmentWidth + segmentGap)), 0, segmentWidth, 4);
               seg++;
            }
            progressBg.endFill();
         }
         else
         {
            progressBg.beginFill(this.shadeColor(this.m_ThemeColor, 0.15), 1);
            var segFill:int = 0;
            while(segFill < segments)
            {
               progressBg.drawRect(Math.round(segFill * (segmentWidth + segmentGap)), 0, segmentWidth, 2);
               segFill++;
            }
            progressBg.endFill();
         }

         if(this.m_MiniProgressFill != null)
         {
            var progressFill:Graphics = this.m_MiniProgressFill.graphics;
            progressFill.clear();
            progressFill.beginFill(this.m_ThemeColor, 1);
            var fillSeg:int = 0;
            while(fillSeg < segments)
            {
               progressFill.drawRect(Math.round(fillSeg * (segmentWidth + segmentGap)), 0, segmentWidth, 2);
               fillSeg++;
            }
            progressFill.endFill();
            this.m_MiniProgressFill.visible = !this.m_MiniLive;
         }

         if(this.m_MiniProgressHead != null)
         {
            var progressHead:Graphics = this.m_MiniProgressHead.graphics;
            progressHead.clear();
            progressHead.beginFill(this.m_ThemeColor, 1);
            progressHead.drawRect(-1, -3, 3, 8);
            progressHead.endFill();
         }
      }

      private function drawMiniArtPlaceholder() : void
      {
         if(this.m_MiniArtHolder == null) return;
         var art:Graphics = this.m_MiniArtHolder.graphics;
         art.clear();

         art.beginFill(this.shadeColor(this.m_ThemeColor, 0.15), 1);
         art.drawRect(0, 0, MINI_ART_SIZE, MINI_ART_SIZE);
         art.endFill();

         art.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.6), 1);
         art.drawRect(0, 0, MINI_ART_SIZE, MINI_ART_SIZE);

         if(this.m_MiniLive)
         {
            this.ensureLiveDisc();
            return;
         }

         this.setLiveDiscVisible(false);

         art.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.5), 0.9);
         art.drawCircle(MINI_ART_SIZE / 2, MINI_ART_SIZE / 2, 25);
         art.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.8), 1);
         art.drawCircle(MINI_ART_SIZE / 2, MINI_ART_SIZE / 2, 18);
         art.beginFill(this.shadeColor(this.m_ThemeColor, 0.8), 1);
         art.drawCircle(MINI_ART_SIZE / 2, MINI_ART_SIZE / 2, 3);
         art.endFill();
      }

      private function ensureLiveDisc() : void
      {
         if(this.m_LiveDisc == null)
         {
            this.m_LiveDisc = new Sprite();
            this.m_LiveDisc.mouseEnabled = false;
            this.m_LiveDisc.x = MINI_RECORD_X;
            this.m_LiveDisc.y = MINI_RECORD_Y;
            this.m_MiniArtHolder.addChild(this.m_LiveDisc);

            this.m_LiveDiscOverlay = new Sprite();
            this.m_LiveDiscOverlay.mouseEnabled = false;
            this.m_MiniArtHolder.addChild(this.m_LiveDiscOverlay);
            this.m_LiveDiscTheme = 0xFFFFFFFF;
         }

         if(this.m_LiveDiscTheme != this.m_ThemeColor)
         {
            this.m_LiveDiscTheme = this.m_ThemeColor;
            this.drawLiveDisc();
            this.drawLiveDiscOverlay();
         }

         this.setLiveDiscVisible(true);
      }

      private function drawLiveDisc() : void
      {
         if(this.m_LiveDisc == null) return;

         var disc:Graphics = this.m_LiveDisc.graphics;
         disc.clear();

         var radius:Number = MINI_RECORD_RADIUS;
         disc.beginFill(0x000000, 0.45);
         disc.drawCircle(1, 2, radius);
         disc.endFill();

         disc.beginFill(this.shadeColor(this.m_ThemeColor, 0.06), 1);
         disc.drawCircle(0, 0, radius);
         disc.endFill();

         disc.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.45), 0.9);
         disc.drawCircle(0, 0, radius);

         var groove:int = 0;
         while(groove < 5)
         {
            var grooveRadius:Number = 15 + groove * 2.2;
            disc.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.3), 0.55);
            this.drawDiscArc(disc, 0, 0, grooveRadius, 285, 335);
            this.drawDiscArc(disc, 0, 0, grooveRadius, 105, 155);
            groove++;
         }

         disc.beginFill(this.shadeColor(this.m_ThemeColor, 0.55), 1);
         disc.drawCircle(0, 0, 11);
         disc.endFill();
         disc.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.9), 0.9);
         disc.drawCircle(0, 0, 11);

         disc.beginFill(0x000000, 1);
         disc.drawCircle(0, 0, 2.2);
         disc.endFill();
      }

      private function drawDiscArc(target:Graphics, centerX:Number, centerY:Number, radius:Number, fromAngle:int, toAngle:int) : void
      {
         var angle:int = fromAngle;
         var started:Boolean = false;
         while(angle <= toAngle)
         {
            var radians:Number = angle * Math.PI / 180;
            var x:Number = centerX + Math.cos(radians) * radius;
            var y:Number = centerY + Math.sin(radians) * radius;
            if(!started)
            {
               target.moveTo(x, y);
               started = true;
            }
            else target.lineTo(x, y);
            angle += 5;
         }
      }

      private function drawLiveDiscOverlay() : void
      {
         if(this.m_LiveDiscOverlay == null) return;
         var overlay:Graphics = this.m_LiveDiscOverlay.graphics;
         overlay.clear();

         var centerX:Number = MINI_RECORD_X;
         var centerY:Number = MINI_RECORD_Y;
         var radius:Number = MINI_RECORD_RADIUS;
         var inner:Number = radius - 5;

         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 1), 0.12);
         var angle:int = 200;
         while(angle <= 250)
         {
            var radians:Number = angle * Math.PI / 180;
            var x:Number = centerX + Math.cos(radians) * radius;
            var y:Number = centerY + Math.sin(radians) * radius;
            if(angle == 200) overlay.moveTo(x, y);
            else overlay.lineTo(x, y);
            angle += 5;
         }
         angle = 250;
         while(angle >= 200)
         {
            radians = angle * Math.PI / 180;
            overlay.lineTo(centerX + Math.cos(radians) * inner, centerY + Math.sin(radians) * inner);
            angle -= 5;
         }
         overlay.endFill();

         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 0.1), 0.95);
         overlay.drawRoundRect(51, 1, 15, 10, 2);
         overlay.endFill();
         overlay.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.3), 0.85);
         overlay.drawRoundRect(51, 1, 15, 10, 2);

         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 0.8), 1);
         overlay.drawRoundRect(54.5, 3.5, 10, 5, 1.5);
         overlay.endFill();
         overlay.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.4), 0.9);
         overlay.drawRoundRect(54.5, 3.5, 10, 5, 1.5);

         overlay.lineStyle(3.5, 0x000000, 0.85);
         overlay.moveTo(59.5, 9);
         overlay.lineTo(59.5, 20);
         overlay.lineTo(47, 48);
         overlay.lineStyle(2, this.shadeColor(this.m_ThemeColor, 0.85), 1);
         overlay.moveTo(59.5, 9);
         overlay.lineTo(59.5, 20);
         overlay.lineTo(47, 48);

         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 0.85), 1);
         overlay.drawCircle(47, 48, 3);
         overlay.endFill();
         overlay.beginFill(0x000000, 0.9);
         overlay.drawCircle(47, 48, 1.4);
         overlay.endFill();
         overlay.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.9), 1);
         overlay.moveTo(47, 48);
         overlay.lineTo(45, 50);
         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 1), 1);
         overlay.drawCircle(44.8, 50.2, 0.8);
         overlay.endFill();

         overlay.beginFill(this.shadeColor(this.m_ThemeColor, 0.35), 1);
         overlay.drawCircle(62, 42, 2);
         overlay.drawCircle(62, 50, 2);
         overlay.endFill();
         overlay.lineStyle(1, this.shadeColor(this.m_ThemeColor, 0.5), 0.6);
         overlay.drawCircle(62, 42, 2);
         overlay.drawCircle(62, 50, 2);
      }

      private function setLiveDiscVisible(value:Boolean) : void
      {
         if(this.m_LiveDisc != null) this.m_LiveDisc.visible = value;
         if(this.m_LiveDiscOverlay != null) this.m_LiveDiscOverlay.visible = value;
      }

      private function createMiniField(size:uint, y:Number, height:Number) : TextField
      {
         var field:TextField = new TextField();
         field.x = MINI_TEXT_X;
         field.y = y;
         field.width = MINI_TEXT_WIDTH;
         field.height = height;
         field.selectable = false;
         field.mouseEnabled = false;
         field.multiline = false;
         field.wordWrap = false;
         field.embedFonts = false;

         var shadow:DropShadowFilter = new DropShadowFilter(2, 45, 0x000000, 0.8, 2, 2, 1, 1);
         field.filters = [shadow];

         return field;
      }

      private function layoutMiniPlayer() : void
      {
         if(this.m_MiniPlayer == null) return;
         var stageWidth:Number = this.stage != null ? this.stage.stageWidth : 1280;
         var stageHeight:Number = this.stage != null ? this.stage.stageHeight : 720;
         if(isNaN(stageWidth) || stageWidth <= 0) stageWidth = 1280;
         if(isNaN(stageHeight) || stageHeight <= 0) stageHeight = 720;

         var x:Number = Math.round(stageWidth * FALLBACK_HUD_X / 100);
         var y:Number = Math.round(stageHeight * FALLBACK_HUD_Y / 100);

         var anchor:DisplayObject = this.findToastAnchor();
         if(anchor != null)
         {
            var anchorPoint:Point = anchor.localToGlobal(new Point(0,0));
            anchorPoint = this.globalToLocal(anchorPoint);
            x = Math.round(anchorPoint.x);
            y = Math.round(anchorPoint.y);
         }
         this.m_BaseX = x;
         this.m_BaseY = y;
         this.applyMiniOffset();
      }

      private function applyMiniOffset() : void
      {
         if(this.m_MiniPlayer == null) return;
         var stageWidth:Number = this.stage != null ? this.stage.stageWidth : 1280;
         var stageHeight:Number = this.stage != null ? this.stage.stageHeight : 720;
         if(isNaN(stageWidth) || stageWidth <= 0) stageWidth = 1280;
         if(isNaN(stageHeight) || stageHeight <= 0) stageHeight = 720;

         var scale:Number = this.m_CardScale / 100;
         var x:Number = Math.max(0, Math.min(Math.max(0, stageWidth - MINI_WIDTH * scale), this.m_BaseX + this.m_OffsetX));
         var y:Number = Math.max(0, Math.min(Math.max(0, stageHeight - MINI_HEIGHT * scale), this.m_BaseY + this.m_OffsetY));
         this.m_MiniPlayer.x = Math.round(x);
         this.m_MiniPlayer.y = Math.round(y);
         if(this.m_EditMode)
         {
            this.m_OffsetX = this.m_MiniPlayer.x - this.m_BaseX;
            this.m_OffsetY = this.m_MiniPlayer.y - this.m_BaseY;
            this.placeEditHint();
         }
      }

      private function findToastAnchor() : DisplayObject
      {
         var node:DisplayObject = this;
         var depth:uint = 0;
         while(node != null && depth < 8)
         {
            var group:Object = this.memberOf(node, "HUDNotificationsGroup_mc");
            if(group != null)
            {
               this.m_HudRoot = node;
               var messages:Object = this.memberOf(group, "Messages_mc");
               if(messages is DisplayObject)
               {
                  this.m_MessagesObject = messages;
                  return messages as DisplayObject;
               }
               var prompt:Object = this.memberOf(group, "PromptMessageHolder_mc");
               if(prompt is DisplayObject) return prompt as DisplayObject;
            }
            node = this.memberOf(node, "parent") as DisplayObject;
            depth++;
         }
         return null;
      }

      private function updateNotificationState() : void
      {
         if(!this.m_KeepNowPlaying)
         {
            this.m_NotificationActive = false;
            return;
         }
         if(this.m_HudRoot == null) this.findToastAnchor();
         var active:Boolean = false;
         if(this.m_MessagesObject is DisplayObject)
         {
            var messages:DisplayObject = this.m_MessagesObject as DisplayObject;
            if(this.isDisplayShown(messages) && this.hasVisibleChild(messages)) active = true;
         }
         this.m_NotificationActive = active;
      }

      private function isDisplayShown(node:DisplayObject) : Boolean
      {
         var current:DisplayObject = node;
         var depth:uint = 0;
         while(current != null && depth < 10)
         {
            if(!current.visible || current.alpha <= 0.01) return false;
            if(current.scaleX <= 0.01 || current.scaleY <= 0.01) return false;
            current = current.parent;
            depth++;
         }
         return true;
      }

      private function hasVisibleChild(node:DisplayObject) : Boolean
      {
         var container:DisplayObjectContainer = node as DisplayObjectContainer;
         if(container == null) return false;
         var index:int = 0;
         while(index < container.numChildren)
         {
            var child:DisplayObject = container.getChildAt(index);
            if(child.visible && child.alpha > 0.05 && (child.scaleX > 0.01 || child.scaleY > 0.01)) return true;
            index++;
         }
         return false;
      }

      private function onCardFadeTick(param1:TimerEvent) : void
      {
         if(this.m_MiniPlayer == null) return;
         this.m_InputTicks++;
         if(this.m_EditMode) this.updateLayoutEdit();
         else if(this.m_InputTicks % INPUT_POLL_TICKS == 0) this.pollLayoutHotkey();
         this.m_NotifyScanTicks++;
         if(this.m_KeepNowPlaying && this.m_NotifyScanTicks % 3 == 0) this.updateNotificationState();

         var shown:Boolean = this.m_EditMode || (this.m_CardShouldShow && !(this.m_KeepNowPlaying && this.m_NotificationActive) && !this.m_CardHidden && !this.hiddenByMode());
         var target:Number = shown ? this.cardAlpha() : 0;
         var alpha:Number = this.m_MiniPlayer.alpha;

         if(alpha < target)
         {
            alpha = Math.min(target, alpha + 0.15);
         }
         else if(alpha > target)
         {
            alpha = Math.max(target, alpha - 0.25);
            if(alpha < 0.2) alpha = 0;
         }

         if(alpha <= 0.01)
         {
            this.m_MiniPlayer.alpha = 0;
            this.m_MiniPlayer.visible = false;
            return;
         }

         this.m_MiniPlayer.alpha = alpha;
         if(this.m_CardShouldShow || this.m_EditMode) this.m_MiniPlayer.visible = true;

         if(this.m_LiveDisc != null && this.m_LiveDisc.visible && this.m_MiniPlayer.visible)
         {
            this.m_LiveDisc.rotation = (this.m_LiveDisc.rotation + 5) % 360;
         }
      }

      private function pollNowPlaying() : void
      {
         this.m_StateTicks++;
         if(this.m_StateTicks % STATE_POLL_TICKS != 0) return;

         if(this.m_StateLoading)
         {
            if(new Date().getTime() - this.m_StateRequestedAt < 4000) return;
            this.abortStateLoad();
            this.m_StateAttempt++;
         }
         this.requestStateDocument();
      }

      private function requestStateDocument() : void
      {
         if(this.m_StateAttempt >= STATE_PATHS.length) this.m_StateAttempt = 0;
         this.m_StateLoading = true;
         this.m_StateRequestedAt = new Date().getTime();
         this.m_StateLoader = new URLLoader();
         this.m_StateLoader.addEventListener(Event.COMPLETE, this.onStateDocument);
         this.m_StateLoader.addEventListener(IOErrorEvent.IO_ERROR, this.onStateDocumentError);
         this.m_StateLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onStateDocumentError);
         try { this.m_StateLoader.load(new URLRequest(STATE_PATHS[this.m_StateAttempt])); }
         catch(loadError:Error) { this.onStateDocumentError(new IOErrorEvent(IOErrorEvent.IO_ERROR)); }
      }

      private function abortStateLoad() : void
      {
         if(this.m_StateLoader != null)
         {
            this.m_StateLoader.removeEventListener(Event.COMPLETE, this.onStateDocument);
            this.m_StateLoader.removeEventListener(IOErrorEvent.IO_ERROR, this.onStateDocumentError);
            this.m_StateLoader.removeEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onStateDocumentError);
            try { this.m_StateLoader.close(); } catch(closeError:Error) {}
         }
         this.m_StateLoader = null;
         this.m_StateLoading = false;
      }

      private function onStateDocumentError(param1:Event) : void
      {
         this.abortStateLoad();
         this.m_StateAttempt++;
         if(this.m_StateAttempt >= STATE_PATHS.length) this.m_StateAttempt = 0;
      }

      private function onStateDocument(param1:Event) : void
      {
         if(!this.m_StateLoading) return;
         var document:String = "";
         try { document = String(this.m_StateLoader.data); } catch(dataError:Error) { document = ""; }
         this.abortStateLoad();

         if(document.length == 0 || document == "undefined" || document == "null")
         {
            this.m_StateAttempt++;
            if(this.m_StateAttempt >= STATE_PATHS.length) this.m_StateAttempt = 0;
            return;
         }
         this.m_StateAttempt = 0;
         this.applyNowPlayingDocument(document);
      }

      private function applyNowPlayingDocument(document:String) : void
      {
         var title:String = this.readStringField(document, "title");
         var artist:String = this.readStringField(document, "artist");
         var state:String = this.readStringField(document, "state");

         this.m_KeepNowPlaying = this.readNumberField(document, "uiKeepNowPlaying") > 0;
         this.applyCardOptions(document);
         this.m_MiniState = state;
         this.m_MiniPositionMs = this.readNumberField(document, "positionMs");
         this.m_MiniDurationMs = this.readNumberField(document, "durationMs");
         this.m_MiniCapturedMs = this.readNumberField(document, "capturedMs");
         this.applyStateTheme(this.readStringField(document, "uiTheme"));
         this.m_MiniLive = this.readNumberField(document, "stream") > 0;

         var station:String = this.readStringField(document, "station");
         this.m_MiniStation = "";
         var displayTitle:String = title;
         var displayArtist:String = artist;

         if(this.m_MiniLive)
         {
            var hasTrack:Boolean = title.length > 0 && (station.length == 0 || title != station);
            if(hasTrack)
            {
               if(displayArtist == "Internet Stream") displayArtist = "Live internet radio";
               this.m_MiniStation = this.shorten(station, 32);
            }
            else
            {
               displayTitle = station.length > 0 ? station : title;
               displayArtist = "Live internet radio";
            }
         }

         var signature:String = title + "\n" + artist;
         if(!this.m_ToastBaselined)
         {
            this.m_ToastBaselined = true;
            this.m_SeenSignature = signature;
            if(!this.m_KeepNowPlaying)
            {
               this.m_AnnouncedSignature = signature;
               return;
            }
         }

         this.m_SeenSignature = signature;
         if(displayTitle.length == 0)
         {
            this.m_AnnouncedSignature = "";
            this.hideMiniPlayer();
            return;
         }

         if(this.m_KeepNowPlaying)
         {
            if(state != "playing" || this.m_TunedAway)
            {
               this.m_AnnouncedSignature = "";
               this.hideMiniPlayer();
               return;
            }
            if(signature != this.m_AnnouncedSignature || !this.m_CardShouldShow)
            {
               this.m_AnnouncedSignature = signature;
               this.showMiniPlayer(displayTitle, displayArtist);
            }
            return;
         }

         if(state != "playing") return;
         if(signature == this.m_AnnouncedSignature) return;
         this.m_AnnouncedSignature = signature;
         this.showMiniPlayer(displayTitle, displayArtist);
      }

      private function showMiniPlayer(title:String, artist:String) : void
      {
         if(this.m_MiniPlayer == null) return;

         this.m_MiniTitle.text = title;
         this.m_MiniTitle.width = this.m_MiniTitle.textWidth + 6;
         this.m_MiniArtist.text = artist.length > 0 ? artist : "Unknown artist";
         this.m_MiniLabel.text = this.m_MiniLive ? "ON AIR" : "NOW PLAYING";

         if(this.m_MiniLive)
         {
            this.m_MiniTime.text = this.m_MiniStation;
         }
         else
         {
            this.m_MiniTime.text = "";
         }

         this.applyMiniFonts();
         this.m_MiniTitle.x = MINI_TEXT_X;
         this.m_MiniMarqueeTick = 0;
         this.m_MiniMarqueeHold = 0;

         if(this.m_MiniLive)
         {
            this.abortMiniArtwork();
            if(this.m_MiniArt != null && this.m_MiniArt.parent != null) this.m_MiniArt.parent.removeChild(this.m_MiniArt);
            this.m_MiniArt = null;
            this.m_ArtSignature = "";
            this.drawMiniArtPlaceholder();
         }
         else
         {
            var signature:String = title + "\n" + artist;
            if(signature != this.m_ArtSignature) this.loadMiniArtwork(signature);
         }

         this.m_ArtRetryTicks = 0;
         this.drawMiniPlayerChrome();
         this.layoutMiniPlayer();
         this.m_CardShouldShow = true;
         this.m_MiniPlayer.visible = true;
         if(!this.m_NotificationActive) this.m_MiniPlayer.alpha = this.cardAlpha();
         this.m_MiniHideAt = this.m_KeepNowPlaying ? 0 : new Date().getTime() + MINI_HOLD_MS;
         this.updateMiniProgress();
      }

      private function updateMiniTitleMarquee() : void
      {
         if(this.m_MiniTitle == null || this.m_MiniPlayer == null || !this.m_MiniPlayer.visible) return;

         var travel:Number = this.m_MiniTitle.textWidth - MINI_TEXT_WIDTH;
         if(travel <= 0)
         {
            this.m_MiniTitle.x = MINI_TEXT_X;
            this.m_MiniMarqueeTick = 0;
            this.m_MiniMarqueeHold = 0;
            return;
         }

         this.m_MiniMarqueeTick++;
         if(this.m_MiniMarqueeTick <= 20) return;

         if(this.m_MiniMarqueeHold > 0)
         {
            this.m_MiniMarqueeHold--;
            if(this.m_MiniMarqueeHold == 0)
            {
               this.m_MiniTitle.x = MINI_TEXT_X;
               this.m_MiniMarqueeTick = 0;
            }
            return;
         }

         var offset:Number = MINI_TEXT_X - this.m_MiniTitle.x;
         if(offset >= travel)
         {
            this.m_MiniTitle.x = MINI_TEXT_X - travel;
            this.m_MiniMarqueeHold = 60;
            return;
         }
         this.m_MiniTitle.x -= 2;
      }

      private function loadMiniArtwork(signature:String) : void
      {
         this.setLiveDiscVisible(false);
         this.abortMiniArtwork();
         if(this.m_MiniArt != null && this.m_MiniArt.parent != null)
         {
            this.m_MiniArt.parent.removeChild(this.m_MiniArt);
         }
         this.m_MiniArt = null;
         this.m_ArtSignature = signature;
         this.m_ArtAttempt = 0;
         this.requestMiniArtwork();
      }

      private function requestMiniArtwork() : void
      {
         if(this.m_ArtAttempt >= ART_PATHS.length)
         {
            this.m_ArtLoading = false;
            return;
         }

         this.m_ArtLoading = true;
         this.m_ArtLoader = new URLLoader();
         this.m_ArtLoader.addEventListener(Event.COMPLETE, this.onMiniArtwork);
         this.m_ArtLoader.addEventListener(IOErrorEvent.IO_ERROR, this.onMiniArtworkError);
         this.m_ArtLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onMiniArtworkError);
         try
         {
            this.m_ArtLoader.load(new URLRequest(ART_PATHS[this.m_ArtAttempt]));
         }
         catch(artLoadError:Error)
         {
            this.onMiniArtworkError(new IOErrorEvent(IOErrorEvent.IO_ERROR));
         }
      }

      private function abortMiniArtwork() : void
      {
         if(this.m_ArtLoader == null) return;
         try
         {
            this.m_ArtLoader.removeEventListener(Event.COMPLETE, this.onMiniArtwork);
            this.m_ArtLoader.removeEventListener(IOErrorEvent.IO_ERROR, this.onMiniArtworkError);
            this.m_ArtLoader.removeEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onMiniArtworkError);
            this.m_ArtLoader.close();
         }
         catch(closeError:Error) {}
         this.m_ArtLoader = null;
         this.m_ArtLoading = false;
      }

      private function onMiniArtworkError(param1:Event) : void
      {
         this.abortMiniArtwork();
         this.m_ArtAttempt++;
         this.requestMiniArtwork();
      }

      private function onMiniArtwork(param1:Event) : void
      {
         if(!this.m_ArtLoading) return;
         var document:String = "";
         try { document = String(this.m_ArtLoader.data); } catch(dataError:Error) {}
         this.abortMiniArtwork();

         if(document.length == 0)
         {
            this.m_ArtAttempt++;
            this.requestMiniArtwork();
            return;
         }

         var newline:int = document.indexOf("\n");
         if(newline <= 0)
         {
            this.m_ArtAttempt++;
            this.requestMiniArtwork();
            return;
         }

         var header:String = document.substring(0, newline).split("\r").join("");
         var space:int = header.indexOf(" ");
         if(space <= 0)
         {
            this.m_ArtAttempt++;
            this.requestMiniArtwork();
            return;
         }

         var width:int = int(header.substring(0, space));
         var height:int = int(header.substring(space + 1));
         if(width <= 0 || height <= 0 || width > 512 || height > 512)
         {
            this.m_ArtAttempt++;
            this.requestMiniArtwork();
            return;
         }

         var decoded:ByteArray = this.decodeBase64(document.substring(newline + 1));
         if(decoded.length != width * height * 4)
         {
            this.m_ArtAttempt++;
            this.requestMiniArtwork();
            return;
         }

         var pixels:BitmapData = null;
         try
         {
            pixels = new BitmapData(width, height);
            decoded.position = 0;
            var row:uint = 0;
            while(row < height)
            {
               var column:uint = 0;
               while(column < width)
               {
                  decoded.readUnsignedByte();
                  var red:uint = decoded.readUnsignedByte();
                  var green:uint = decoded.readUnsignedByte();
                  var blue:uint = decoded.readUnsignedByte();
                  pixels.setPixel(column, row, (red << 16) | (green << 8) | blue);
                  column++;
               }
               row++;
            }
         }
         catch(pixelError:Error)
         {
            pixels = null;
         }

         this.m_ArtLoading = false;
         if(pixels == null)
         {
            this.drawMiniArtworkMosaic(decoded, width, height);
            return;
         }

         var bitmap:Bitmap = new Bitmap(pixels);
         bitmap.smoothing = true;
         this.finishMiniArtwork(bitmap, width, height);
      }

      private function finishMiniArtwork(image:DisplayObject, width:Number, height:Number) : void
      {
         if(image == null) return;
         this.setLiveDiscVisible(false);
         var scale:Number = MINI_ART_SIZE / width;
         image.scaleX = scale;
         image.scaleY = scale;
         image.x = (MINI_ART_SIZE - width * scale) / 2;
         image.y = (MINI_ART_SIZE - height * scale) / 2;
         this.m_MiniArt = image;
         image.alpha = 0.15;
         this.m_MiniArtHolder.addChild(image);
      }

      private function drawMiniArtworkMosaic(pixels:ByteArray, width:int, height:int) : void
      {
         var art:Sprite = new Sprite();
         var drawing:Graphics = art.graphics;
         var size:int = int(MINI_ART_SIZE);
         var row:int = 0;
         while(row < size)
         {
            var sourceRow:int = int(row * height / size);
            var column:int = 0;
            while(column < size)
            {
               var sourceColumn:int = int(column * width / size);
               pixels.position = (sourceRow * width + sourceColumn) * 4;
               pixels.readUnsignedByte();
               var red:int = pixels.readUnsignedByte();
               var green:int = pixels.readUnsignedByte();
               var blue:int = pixels.readUnsignedByte();
               drawing.beginFill((red << 16) | (green << 8) | blue, 1);
               drawing.drawRect(column, row, 1, 1);
               drawing.endFill();
               column++;
            }
            row++;
         }
         this.finishMiniArtwork(art, size, size);
      }

      private function decodeBase64(param1:String) : ByteArray
      {
         var output:ByteArray = new ByteArray();
         var buffer:uint = 0;
         var bits:uint = 0;
         var index:uint = 0;
         var length:uint = param1.length;
         while(index < length)
         {
            var code:int = param1.charCodeAt(index++);
            var value:int = -1;
            if(code >= 65 && code <= 90) value = code - 65;
            else if(code >= 97 && code <= 122) value = code - 71;
            else if(code >= 48 && code <= 57) value = code + 4;
            else if(code == 43) value = 62;
            else if(code == 47) value = 63;
            else continue;

            buffer = (buffer << 6) | uint(value);
            bits += 6;
            if(bits >= 8)
            {
               bits -= 8;
               output.writeByte((buffer >> bits) & 0xFF);
            }
         }
         return output;
      }

      private function hideMiniPlayer() : void
      {
         this.m_CardShouldShow = false;
         if(this.m_EditMode) return;
         if(this.m_MiniPlayer != null)
         {
            this.m_MiniPlayer.visible = false;
            this.m_MiniPlayer.alpha = 0;
         }
         this.m_MiniHideAt = 0;
      }

      private function updateMiniPlayer() : void
      {
         if(this.m_MiniPlayer == null || !this.m_MiniPlayer.visible || this.m_EditMode) return;
         if(this.m_MiniState != "playing")
         {
            this.hideMiniPlayer();
            return;
         }
         if(!this.m_KeepNowPlaying && this.m_MiniHideAt > 0 && new Date().getTime() >= this.m_MiniHideAt)
         {
            this.hideMiniPlayer();
            return;
         }

         if(this.m_MiniLive && this.m_LiveDisc == null) this.drawMiniArtPlaceholder();

         if(this.m_MiniArt != null && this.m_MiniArt.alpha < 1)
         {
            this.m_MiniArt.alpha = Math.min(1, this.m_MiniArt.alpha + 0.2);
         }

         this.updateMiniProgress();
         this.updateMiniTitleMarquee();

         if(!this.m_MiniLive && this.m_MiniArt == null && !this.m_ArtLoading)
         {
            this.m_ArtRetryTicks++;
            if(this.m_ArtRetryTicks >= 8)
            {
               this.m_ArtRetryTicks = 0;
               this.m_ArtAttempt = 0;
               this.requestMiniArtwork();
            }
         }
      }

      private function updateMiniProgress() : void
      {
         if(this.m_MiniProgressFill == null) return;
         if(this.m_MiniLive || this.m_MiniDurationMs <= 0)
         {
            this.m_MiniProgressFill.scaleX = 0;
            if(this.m_MiniProgressHead != null) this.m_MiniProgressHead.visible = false;
            if(!this.m_MiniLive) this.updateMiniTime(0);
            return;
         }

         var position:Number = this.currentPositionMs();
         if(position < 0) position = 0;
         if(position > this.m_MiniDurationMs) position = this.m_MiniDurationMs;

         var ratio:Number = position / this.m_MiniDurationMs;
         this.m_MiniProgressFill.scaleX = ratio;

         if(this.m_MiniProgressHead != null)
         {
            this.m_MiniProgressHead.visible = true;
            this.m_MiniProgressHead.x = MINI_TEXT_X + MINI_TEXT_WIDTH * ratio - 1;
         }
         this.updateMiniTime(position);
      }

      private function currentPositionMs() : Number
      {
         var position:Number = this.m_MiniPositionMs;
         if(this.m_MiniState == "playing" && this.m_MiniCapturedMs > 0)
         {
            var elapsed:Number = new Date().getTime() - this.m_MiniCapturedMs;
            if(elapsed > 0) position += elapsed;
         }
         return position;
      }

      private function updateMiniTime(position:Number) : void
      {
         if(this.m_MiniTime == null || this.m_MiniLive) return;
         var text:String = "";
         if(this.m_MiniDurationMs > 0)
         {
            text = this.formatTime(position) + " / " + this.formatTime(this.m_MiniDurationMs);
         }
         if(this.m_MiniTime.text != text) this.m_MiniTime.text = text;
      }

      private function formatTime(milliseconds:Number) : String
      {
         if(isNaN(milliseconds) || milliseconds < 0) milliseconds = 0;
         var total:int = int(milliseconds / 1000);
         var hours:int = int(total / 3600);
         var minutes:int = int(total / 60) % 60;
         var seconds:int = total % 60;
         var text:String = "";
         if(hours > 0)
         {
            text = hours + ":";
            if(minutes < 10) text += "0";
         }
         text += minutes;
         text += ":";
         if(seconds < 10) text += "0";
         text += seconds;
         return text;
      }

      private function shorten(text:String, limit:int) : String
      {
         if(text.length <= limit) return text;
         return text.substring(0, limit - 3) + "...";
      }

      private function applyMiniFonts() : void
      {
         var labelFormat:TextFormat = new TextFormat("$MAIN_Font_Bold", 10, this.shadeColor(this.m_ThemeColor, 0.8), true);
         labelFormat.letterSpacing = 1.5;
         this.applyFormat(this.m_MiniLabel, labelFormat);

         var face:String = this.m_CjkFontReady && this.m_CjkFontName.length > 0 ? this.m_CjkFontName : "$MAIN_Font_Bold";
         this.applyFormat(this.m_MiniTitle, new TextFormat(face, 16, this.m_ThemeColor, true));

         var artistFace:String = this.m_CjkFontReady && this.m_CjkFontName.length > 0 ? this.m_CjkFontName : "$MAIN_Font";
         this.applyFormat(this.m_MiniArtist, new TextFormat(artistFace, 12, this.shadeColor(this.m_ThemeColor, 0.75), false));

         var timeFormat:TextFormat = new TextFormat("$MAIN_Font", 9, this.shadeColor(this.m_ThemeColor, 0.6), false);
         timeFormat.align = "right";
         this.applyFormat(this.m_MiniTime, timeFormat);
      }

      private function applyCardTheme() : void
      {
         this.drawMiniPlayerChrome();
         this.applyMiniFonts();
      }

      private function applyStateTheme(theme:String) : void
      {
         if(theme.length != 6 || theme == this.m_PipboyTheme) return;
         var value:uint = parseInt(theme, 16);
         if(isNaN(value) || value == 0) return;
         this.m_PipboyTheme = theme;
         this.m_ThemeColor = value;
         this.applyCardTheme();
      }

      private function pollPipboyTheme() : void
      {
         if(!this.m_StorageReady || this.m_Storage == null) return;
         var document:String;
         try { document = String(this.m_Storage.load(THEME_STORAGE_NAME)); }
         catch(themeError:Error) { return; }
         if(document == null || document.length == 0 || document == "undefined" || document == "null") return;

         var color:String = this.readStringField(document, "color");
         if(color.length != 6 || color == this.m_PipboyTheme) return;

         var value:uint = parseInt(color, 16);
         if(isNaN(value)) return;

         this.m_PipboyTheme = color;
         this.m_ThemeColor = value;
         this.applyCardTheme();
      }

      private function onHudColors(param1:FromClientDataEvent) : void
      {
         if(this.m_PipboyTheme.length > 0) return;
         var colors:Object = param1 != null ? param1.data : null;
         if(colors == null) return;
         var hue:Number = Number(colors.hue);
         var saturation:Number = Number(colors.saturation);
         var brightness:Number = Number(colors.value);
         var contrast:Number = Number(colors.contrast);
         if(hue == 0 && saturation == 0 && brightness == 0 && contrast == 0) return;

         var color:uint = this.computeHudColor(hue, saturation, brightness, contrast);
         if(color == 0) return;
         this.m_ThemeColor = color;
         this.applyCardTheme();
      }

      private function computeHudColor(hue:Number, saturation:Number, brightness:Number, contrast:Number) : uint
      {
         if(isNaN(hue) || isNaN(saturation) || isNaN(brightness) || isNaN(contrast)) return DEFAULT_THEME_COLOR;
         try
         {
            var adjustClass:Class = ApplicationDomain.currentDomain.getDefinition("fl.motion.AdjustColor") as Class;
            if(adjustClass == null) return DEFAULT_THEME_COLOR;
            var adjust:Object = new adjustClass();
            adjust.hue = hue != 0 ? hue : 0.001;
            adjust.saturation = saturation;
            adjust.brightness = brightness != 0 ? brightness : 0.001;
            adjust.contrast = contrast;

            var matrix:Array = adjust.CalculateFinalFlatArray() as Array;
            if(matrix == null || matrix.length < 20) return DEFAULT_THEME_COLOR;

            var red:Number = Number(matrix[0]) * 255 + Number(matrix[1]) * 255 + Number(matrix[2]) * 255 + Number(matrix[3]) * 255 + Number(matrix[4]);
            var green:Number = Number(matrix[5]) * 255 + Number(matrix[6]) * 255 + Number(matrix[7]) * 255 + Number(matrix[8]) * 255 + Number(matrix[9]);
            var blue:Number = Number(matrix[10]) * 255 + Number(matrix[11]) * 255 + Number(matrix[12]) * 255 + Number(matrix[13]) * 255 + Number(matrix[14]);

            red = Math.max(0, Math.min(255, Math.round(red)));
            green = Math.max(0, Math.min(255, Math.round(green)));
            blue = Math.max(0, Math.min(255, Math.round(blue)));

            if(red + green + blue < 60) return DEFAULT_THEME_COLOR;
            return (uint(red) << 16) | (uint(green) << 8) | uint(blue);
         }
         catch(colorError:Error) {}
         return DEFAULT_THEME_COLOR;
      }

      private function shadeColor(color:uint, factor:Number) : uint
      {
         var red:int = Math.round((color >> 16 & 0xFF) * factor);
         var green:int = Math.round((color >> 8 & 0xFF) * factor);
         var blue:int = Math.round((color & 0xFF) * factor);
         red = Math.max(0, Math.min(255, red));
         green = Math.max(0, Math.min(255, green));
         blue = Math.max(0, Math.min(255, blue));
         return (uint(red) << 16) | (uint(green) << 8) | uint(blue);
      }

      private function applyFormat(field:TextField, format:TextFormat) : void
      {
         if(field == null) return;
         field.embedFonts = false;
         field.defaultTextFormat = format;
         field.setTextFormat(format);
      }

      private function updateCjkFont() : void
      {
         if(this.m_CjkFontReady || this.m_CjkFontLoading) return;
         if(this.m_CjkFontWait > 0)
         {
            this.m_CjkFontWait--;
            return;
         }
         if(this.m_CjkFontAttempt >= CJK_FONT_PATHS.length)
         {
            this.m_CjkFontAttempt = 0;
            this.m_CjkFontWait = 25;
            return;
         }

         this.m_CjkFontLoading = true;
         this.m_CjkFontLoader = new Loader();
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(Event.COMPLETE, this.onCjkFontComplete);
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR, this.onCjkFontError);
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onCjkFontError);
         try
         {
            this.m_CjkFontLoader.load(new URLRequest(CJK_FONT_PATHS[this.m_CjkFontAttempt]));
         }
         catch(fontLoadError:Error)
         {
            this.onCjkFontError(new IOErrorEvent(IOErrorEvent.IO_ERROR));
         }
      }

      private function onCjkFontComplete(param1:Event) : void
      {
         if(!this.m_CjkFontLoading) return;
         this.m_CjkFontLoading = false;
         try
         {
            var domain:ApplicationDomain = this.m_CjkFontLoader.contentLoaderInfo.applicationDomain;
            var fontClass:Class = domain.getDefinition("$MAIN_Font") as Class;
            if(fontClass == null) throw new Error("$MAIN_Font missing");
            Font.registerFont(fontClass);
            var font:Font = new fontClass();
            this.m_CjkFontName = font.fontName;
            this.m_CjkFontReady = true;
            this.releaseCjkLoader();
            this.applyMiniFonts();
            return;
         }
         catch(fontError:Error)
         {
            this.m_CjkFontAttempt++;
         }
         this.releaseCjkLoader();
         this.m_CjkFontWait = 10;
      }

      private function onCjkFontError(param1:Event) : void
      {
         this.m_CjkFontLoading = false;
         this.releaseCjkLoader();
         this.m_CjkFontAttempt++;
         if(this.m_CjkFontAttempt >= CJK_FONT_PATHS.length)
         {
            this.m_CjkFontAttempt = 0;
            this.m_CjkFontWait = 50;
         }
         else
         {
            this.m_CjkFontWait = 5;
         }
      }

      private function releaseCjkLoader() : void
      {
         if(this.m_CjkFontLoader == null) return;
         try
         {
            this.m_CjkFontLoader.contentLoaderInfo.removeEventListener(Event.COMPLETE, this.onCjkFontComplete);
            this.m_CjkFontLoader.contentLoaderInfo.removeEventListener(IOErrorEvent.IO_ERROR, this.onCjkFontError);
            this.m_CjkFontLoader.contentLoaderInfo.removeEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onCjkFontError);
            this.m_CjkFontLoader.close();
         }
         catch(closeError:Error) {}
         this.m_CjkFontLoader = null;
      }

      private function readStringField(document:String, key:String) : String
      {
         var marker:String = "\"" + key + "\":\"";
         var start:int = document.indexOf(marker);
         if(start < 0) return "";
         start += marker.length;
         var result:String = "";
         var index:int = start;
         while(index < document.length)
         {
            var character:String = document.charAt(index);
            if(character == "\\")
            {
               index++;
               if(index >= document.length) break;
               var escape:String = document.charAt(index);
               if(escape == "n") result += " ";
               else if(escape == "r" || escape == "t") result += " ";
               else if(escape == "u" && index + 4 < document.length)
               {
                  var code:int = parseInt(document.substr(index + 1, 4), 16);
                  if(!isNaN(code)) result += String.fromCharCode(code);
                  index += 4;
               }
               else result += escape;
            }
            else if(character == "\"") break;
            else result += character;
            index++;
         }
         return result;
      }

      private function readNumberField(document:String, key:String) : Number
      {
         var marker:String = "\"" + key + "\":";
         var start:int = document.indexOf(marker);
         if(start < 0) return 0;
         start += marker.length;
         var end:int = start;
         while(end < document.length)
         {
            var character:String = document.charAt(end);
            if(character == "," || character == "}") break;
            end++;
         }
         var value:Number = Number(document.substring(start, end));
         return isNaN(value) || value < 0 ? 0 : value;
      }

      private function onReceiveMessage(sender:String, msg:String) : void
      {
         this.ensureStorage();
         if (!this.m_StorageReady || this.m_Storage == null) return;

         try
         {
            if (sender != "R76Radio_Pipboy") return;

            var token:String = "";
            var separator:int = msg.indexOf("#");
            if(separator >= 0)
            {
               token = msg.substring(separator + 1);
               msg = msg.substring(0, separator);
            }
            if(token.length > 0 && token == this.m_LastToken) return;
            if(token.length > 0) this.m_LastToken = token;

            var command:String = msg;
            if (command.length > 900) command = command.substring(0, 900);
            command = this.decodeCommand(command);

            if(command == "hidecard")
            {
               this.m_TunedAway = true;
               this.hideMiniPlayer();
               return;
            }
            if(command == "showcard")
            {
               this.m_TunedAway = false;
               return;
            }

            var document:String = "{\"stamp\":" + new Date().getTime() + ",\"command\":\"" + this.sanitize(command) + "\"}";
            if(command.substr(0, 5) == "diag:")
            {
               this.m_Storage.save(DIAG_STORAGE_NAME, document);
               return;
            }
            this.m_Storage.save(document);
         }
         catch(e:Error) {}
      }

      private function decodeCommand(param1:String) : String
      {
         if(param1.substr(0, 9) != "stream%7C" && param1.substr(0, 8) != "media%7C") return param1;
         var result:String = param1.split("%7C").join("|");
         result = result.split("%23").join("#");
         result = result.split("%25").join("%");
         return result;
      }

      private function publishRelayStatus() : void
      {
         if(this.m_RelayPublished || !this.m_StorageReady || this.m_Storage == null) return;
         try
         {
            var document:String = "{\"relay\":\"1\",\"build\":\"" + RELAY_BUILD + "\"}";
            if(this.m_Storage.save(RELAY_STORAGE_NAME, document) === true) this.m_RelayPublished = true;
         }
         catch(relayError:Error) {}
      }

      private function pollSharedCommands() : void
      {
         try
         {
            this.m_Store = SharedObject.getLocal(STORE_NAME);
            if(this.m_Store == null) return;
            if(!this.m_RelayMarked)
            {
               this.m_RelayMarked = true;
               try
               {
                  this.m_Store.data.relay = "1";
                  this.m_Store.data.relayBuild = RELAY_BUILD;
                  this.m_Store.flush();
               }
               catch(markerError:Error) {}
            }
            var stamp:Number = Number(this.m_Store.data.stamp);
            var command:String = this.m_Store.data.command == null ? "" : String(this.m_Store.data.command);
            if(isNaN(stamp) || stamp <= 0 || command.length == 0) return;
            if(!this.m_Baselined)
            {
               this.m_Baselined = true;
               this.m_LastStamp = stamp;
               return;
            }
            if(stamp == this.m_LastStamp) return;

            var document:String = "{\"stamp\":" + stamp + ",\"command\":\"" + this.sanitize(command) + "\"}";
            if(this.m_Storage.save(document) === true) this.m_LastStamp = stamp;
         }
         catch(commandError:Error) {}
      }

      private function markPresence() : void
      {
         if(this.m_PresenceMarked) return;
         try
         {
            var store:SharedObject = SharedObject.getLocal(STORE_NAME);
            if(store == null) return;
            store.data.relayPresent = "1";
            store.data.relayBuild = RELAY_BUILD;
            try { store.flush(); } catch(flushError:Error) {}
            this.m_PresenceMarked = true;
         }
         catch(presenceError:Error) {}
      }

      private function sanitize(param1:String) : String
      {
         var result:String = "";
         var index:uint = 0;
         while(index < param1.length && index < 900)
         {
            var character:String = param1.charAt(index);
            if(character == "\"" || character == "\\" || character == "\n" || character == "\r") result += " ";
            else result += character;
            index++;
         }
         return result;
      }

      private function inputCall(name:String, argument:* = undefined, second:* = undefined) : *
      {
         this.m_InputOk = false;
         if(this.m_Code == null) return undefined;
         try
         {
            var result:*;
            if(argument === undefined) result = this.m_Code.call(name);
            else if(second === undefined) result = this.m_Code.call(name, argument);
            else result = this.m_Code.call(name, argument, second);
            this.m_InputOk = true;
            return result;
         }
         catch(callError:Error) {}
         return undefined;
      }

      private function ensureInput() : Boolean
      {
         if(this.m_InputReady) return true;
         if(this.m_Code == null) this.m_Code = this.resolveBridge();
         if(this.m_Code == null) return false;
         this.m_InputReady = this.registerKey(KEY_SHIFT) && this.registerKey(KEY_CTRL) && this.registerKey(KEY_L);
         return this.m_InputReady;
      }

      private function registerKey(code:uint) : Boolean
      {
         if(this.m_RegisteredKeys[code] === true) return true;
         this.inputCall("Input.RegisterKey", code);
         if(this.m_InputOk) this.m_RegisteredKeys[code] = true;
         return this.m_InputOk;
      }

      private function keyDown(code:uint) : Boolean
      {
         if(this.m_KeyCache[code] !== undefined) return this.m_KeyCache[code];
         var down:Boolean = Boolean(this.inputCall("Input.IsKeyPressed", code));
         this.m_KeyCache[code] = down;
         return down;
      }

      private function layoutChordDown() : Boolean
      {
         return this.keyDown(KEY_L) && this.keyDown(KEY_SHIFT) && this.keyDown(KEY_CTRL);
      }

      private function pollLayoutHotkey() : void
      {
         if(!this.m_InputReady)
         {
            if(this.m_InputBackoff > 0)
            {
               this.m_InputBackoff--;
               return;
            }
            if(!this.ensureInput())
            {
               this.m_InputBackoff = 30;
               return;
            }
         }
         this.m_KeyCache = {};
         var chord:Boolean = this.layoutChordDown();
         var pressed:Boolean = (chord && !this.m_ChordWasDown) || this.padChordPressed();
         this.m_ChordWasDown = chord;
         if(pressed)
         {
            this.beginLayoutEdit();
            return;
         }
         this.pollMediaHotkeys();
      }

      private function padChordPressed() : Boolean
      {
         if(!this.m_PadRegistered)
         {
            this.inputCall("Input.RegisterGamepadButtons", PAD_CHORD, PAD_INDEX);
            this.m_PadRegistered = this.m_InputOk;
            if(!this.m_PadRegistered) return false;
         }
         var down:Boolean = Boolean(this.inputCall("Input.AreGamepadButtonsPressed", PAD_CHORD, PAD_INDEX));
         var pressed:Boolean = down && !this.m_PadChordWasDown;
         this.m_PadChordWasDown = down;
         return pressed;
      }

      private function pollMediaHotkeys() : void
      {
         var index:int = 0;
         while(index < this.m_Hotkeys.length)
         {
            var keys:Array = this.m_Hotkeys[index] as Array;
            var down:Boolean = keys.length > 0;
            var key:int = 0;
            while(key < keys.length)
            {
               if(!this.registerKey(keys[key]) || !this.keyDown(keys[key])) down = false;
               key++;
            }
            if(down && this.m_HotkeyWasDown[index] !== true)
            {
               if(String(HOTKEY_COMMANDS[index]).length > 0) this.sendCommand(HOTKEY_COMMANDS[index]);
               else this.m_CardHidden = !this.m_CardHidden;
            }
            this.m_HotkeyWasDown[index] = down;
            index++;
         }
      }

      private function sendCommand(command:String) : void
      {
         if(!this.m_StorageReady || this.m_Storage == null) return;
         try { this.m_Storage.save("{\"stamp\":" + new Date().getTime() + ",\"command\":\"" + command + "\"}"); }
         catch(commandError:Error) {}
      }

      private function parseHotkeys(spec:String) : void
      {
         this.m_Hotkeys = [];
         this.m_HotkeyWasDown = [];
         var entries:Array = spec.split("|");
         var index:int = 0;
         while(index < HOTKEY_COMMANDS.length)
         {
            var chord:Array = [];
            var text:String = index < entries.length ? String(entries[index]) : "";
            if(text.length > 0)
            {
               var tokens:Array = text.split("+");
               var token:int = 0;
               while(token < tokens.length)
               {
                  var code:uint = this.keyCodeFor(String(tokens[token]));
                  if(code == 0)
                  {
                     chord = [];
                     break;
                  }
                  chord.push(code);
                  token++;
               }
            }
            this.m_Hotkeys.push(chord);
            this.m_HotkeyWasDown.push(false);
            index++;
         }
      }

      private function keyCodeFor(name:String) : uint
      {
         if(name.length == 1)
         {
            var single:uint = name.charCodeAt(0);
            if((single >= 65 && single <= 90) || (single >= 48 && single <= 57)) return single;
            return 0;
         }
         switch(name)
         {
            case "CTRL":
            case "CONTROL": return 17;
            case "SHIFT": return 16;
            case "ALT": return 18;
            case "SPACE": return 32;
            case "LEFT": return 37;
            case "UP": return 38;
            case "RIGHT": return 39;
            case "DOWN": return 40;
         }
         var number:Number = Number(name.substr(1));
         if(name.charAt(0) == "F" && number >= 1 && number <= 12) return 111 + number;
         if(name.indexOf("NUM") == 0)
         {
            var digit:Number = Number(name.substr(name.length - 1));
            if(name.length >= 4 && !isNaN(digit)) return 96 + digit;
         }
         return 0;
      }

      private function applyCardOptions(document:String) : void
      {
         var scale:Number = this.readNumberField(document, "uiScale");
         if(scale < 50 || scale > 200) scale = 100;
         var opacity:Number = this.readNumberField(document, "uiOpacity");
         if(opacity < 20 || opacity > 100) opacity = 100;
         this.m_HideInVats = this.readNumberField(document, "uiVats") > 0;
         this.m_IniScale = scale;
         this.m_IniOpacity = opacity;
         this.refreshCardLook();
         var spec:String = this.readStringField(document, "uiKeys");
         if(spec != this.m_HotkeySpec)
         {
            this.m_HotkeySpec = spec;
            this.parseHotkeys(spec);
         }
      }

      private function refreshCardLook() : void
      {
         var scale:Number = this.m_ScaleOverride > 0 ? this.m_ScaleOverride : this.m_IniScale;
         this.m_CardOpacity = this.m_OpacityOverride > 0 ? this.m_OpacityOverride : this.m_IniOpacity;
         if(scale == this.m_CardScale) return;
         this.m_CardScale = scale;
         if(this.m_MiniPlayer == null) return;
         this.m_MiniPlayer.scaleX = scale / 100;
         this.m_MiniPlayer.scaleY = scale / 100;
         if(this.m_MiniPlayer.visible) this.applyMiniOffset();
      }

      private function adjustScale(amount:Number) : void
      {
         var current:Number = this.m_ScaleOverride > 0 ? this.m_ScaleOverride : this.m_CardScale;
         this.m_ScaleOverride = Math.max(50, Math.min(200, current + amount));
         this.refreshCardLook();
      }

      private function adjustOpacity(amount:Number) : void
      {
         var current:Number = this.m_OpacityOverride > 0 ? this.m_OpacityOverride : this.m_CardOpacity;
         this.m_OpacityOverride = Math.max(20, Math.min(100, current + amount));
         this.refreshCardLook();
      }

      private function cardAlpha() : Number
      {
         return this.m_CardOpacity / 100;
      }

      private function hiddenByMode() : Boolean
      {
         return this.m_HideInVats && (this.m_HudMode == HUDModes.VATS_MODE || this.m_HudMode == HUDModes.VATS_PLAYBACK);
      }

      private function onHudMode(event:FromClientDataEvent) : void
      {
         var data:Object = event != null ? event.data : null;
         if(data == null) return;
         this.m_HudMode = String(data.hudMode);
      }

      private function padAxis(value:*) : Number
      {
         var raw:Number = Number(value) || 0;
         var size:Number = Math.abs(raw);
         if(size <= PAD_DEADZONE) return 0;
         var scaled:Number = Math.min(1, (size - PAD_DEADZONE) / (32767 - PAD_DEADZONE));
         return raw < 0 ? -scaled : scaled;
      }

      private function beginLayoutEdit() : void
      {
         if(this.m_EditMode || this.m_MiniPlayer == null) return;
         if(!Boolean(this.inputCall("Input.BeginMouseCapture"))) return;

         this.m_EditMode = true;
         this.m_PadLastAt = 0;
         this.inputCall("Input.SuppressGamepadAnalog", PAD_INDEX);
         this.inputCall("Input.SuppressGamepadButtons", PAD_DPAD, PAD_INDEX);
         if(this.m_MiniTitle.text.length == 0)
         {
            this.m_MiniLabel.text = "LAYOUT";
            this.m_MiniTitle.text = "76Radio";
            this.m_MiniArtist.text = "Card position";
            this.m_MiniTime.text = "";
            this.applyMiniFonts();
            this.drawMiniArtPlaceholder();
         }
         this.drawMiniPlayerChrome();
         this.buildEditOverlay();
         this.layoutMiniPlayer();
         this.m_MiniPlayer.alpha = this.cardAlpha();
         this.m_MiniPlayer.visible = true;
      }

      private function endLayoutEdit() : void
      {
         if(!this.m_EditMode) return;
         this.m_EditMode = false;
         this.inputCall("Input.EndMouseCapture");
         this.inputCall("Input.UnsuppressGamepadAnalog", PAD_INDEX);
         this.inputCall("Input.UnsuppressGamepadButtons", PAD_DPAD, PAD_INDEX);
         if(this.m_EditFrame != null) this.m_EditFrame.visible = false;
         if(this.m_EditHint != null) this.m_EditHint.visible = false;
         this.saveLayout();
         if(!this.m_CardShouldShow) this.hideMiniPlayer();
      }

      private function updateLayoutEdit() : void
      {
         this.m_KeyCache = {};
         var chord:Boolean = this.layoutChordDown();
         var pressed:Boolean = chord && !this.m_ChordWasDown;
         this.m_ChordWasDown = chord;
         if(pressed || !this.m_InputOk || this.padChordPressed())
         {
            this.endLayoutEdit();
            return;
         }
         this.moveWithPad();

         var mouse:* = this.inputCall("Input.PollMouse");
         if(!this.m_InputOk || mouse == null) return;
         if(this.mouseField(mouse, "capturing", 1) == 0)
         {
            this.endLayoutEdit();
            return;
         }
         if(this.mouseField(mouse, "rightPressed", 0) != 0)
         {
            this.m_OffsetX = 0;
            this.m_OffsetY = 0;
            this.m_ScaleOverride = 0;
            this.m_OpacityOverride = 0;
            this.refreshCardLook();
            this.applyMiniOffset();
            return;
         }
         var wheel:Number = this.mouseField(mouse, "wheel", 0);
         if(wheel != 0)
         {
            var step:Number = wheel > 0 ? LOOK_STEP : -LOOK_STEP;
            if(this.keyDown(KEY_SHIFT)) this.adjustOpacity(step);
            else this.adjustScale(step);
         }
         if(this.mouseField(mouse, "left", 0) == 0) return;

         var dx:Number = this.mouseField(mouse, "dx", 0) * MOUSE_SENSITIVITY;
         var dy:Number = this.mouseField(mouse, "dy", 0) * MOUSE_SENSITIVITY;
         if(dx == 0 && dy == 0) return;
         this.m_OffsetX += dx;
         this.m_OffsetY += dy;
         this.applyMiniOffset();
      }

      private function moveWithPad() : void
      {
         var now:Number = getTimer();
         var elapsed:Number = this.m_PadLastAt > 0 ? Math.min(0.1, (now - this.m_PadLastAt) / 1000) : 0;
         this.m_PadLastAt = now;
         var pad:* = this.inputCall("Input.PollGamepad", PAD_INDEX);
         if(!this.m_InputOk || pad == null || this.mouseField(pad, "connected", 0) == 0) return;
         var buttons:uint = uint(this.mouseField(pad, "buttons", 0));
         var sizeStep:Number = ((buttons & 1) != 0 ? 1 : 0) - ((buttons & 2) != 0 ? 1 : 0);
         var opacityStep:Number = ((buttons & 8) != 0 ? 1 : 0) - ((buttons & 4) != 0 ? 1 : 0);
         if(sizeStep != 0) this.adjustScale(sizeStep * LOOK_PAD_RATE * elapsed);
         if(opacityStep != 0) this.adjustOpacity(opacityStep * LOOK_PAD_RATE * elapsed);
         var dx:Number = this.padAxis(this.mouseField(pad, "rx", 0));
         var dy:Number = this.padAxis(this.mouseField(pad, "ry", 0));
         if(dx == 0 && dy == 0) return;
         this.m_OffsetX += dx * PAD_SPEED * elapsed;
         this.m_OffsetY -= dy * PAD_SPEED * elapsed;
         this.applyMiniOffset();
      }

      private function mouseField(mouse:*, key:String, fallback:Number) : Number
      {
         if(mouse is String)
         {
            var text:String = String(mouse);
            var start:int = text.indexOf("\"" + key + "\"");
            if(start < 0) return fallback;
            start = text.indexOf(":", start);
            if(start < 0) return fallback;
            start++;
            var end:int = start;
            while(end < text.length && text.charAt(end) != "," && text.charAt(end) != "}") end++;
            var token:String = text.substring(start, end).replace(/\s+/g, "");
            if(token == "true") return 1;
            if(token == "false") return 0;
            var parsed:Number = Number(token);
            return isNaN(parsed) ? fallback : parsed;
         }
         var value:* = undefined;
         try { value = mouse[key]; } catch(fieldError:Error) {}
         if(value === undefined || value === null) return fallback;
         if(value is Boolean) return value ? 1 : 0;
         var number:Number = Number(value);
         return isNaN(number) ? fallback : number;
      }

      private function buildEditOverlay() : void
      {
         if(this.m_EditFrame == null)
         {
            this.m_EditFrame = new Sprite();
            this.m_EditFrame.mouseEnabled = false;
            this.m_MiniPlayer.addChild(this.m_EditFrame);

            this.m_EditHint = this.createMiniField(10, -44, 40);
            this.m_EditHint.x = 0;
            this.m_EditHint.width = MINI_WIDTH;
            this.m_EditHint.multiline = true;
            this.m_MiniPlayer.addChild(this.m_EditHint);
         }
         var frame:Graphics = this.m_EditFrame.graphics;
         frame.clear();
         frame.lineStyle(2, this.shadeColor(this.m_ThemeColor, 0.9), 1);
         frame.drawRect(-3, -3, MINI_WIDTH + 6, MINI_HEIGHT + 6);
         this.m_EditFrame.visible = true;

         this.applyFormat(this.m_EditHint, new TextFormat("$MAIN_Font_Bold", 10, this.shadeColor(this.m_ThemeColor, 0.9), true));
         this.m_EditHint.text = EDIT_HINT;
         this.m_EditHint.visible = true;
      }

      private function placeEditHint() : void
      {
         if(this.m_EditHint == null || this.m_MiniPlayer == null) return;
         var scale:Number = this.m_CardScale / 100;
         var size:Number = EDIT_HINT_SCALE / scale;
         this.m_EditHint.scaleX = size;
         this.m_EditHint.scaleY = size;
         this.m_EditHint.y = this.m_MiniPlayer.y >= 52 * EDIT_HINT_SCALE ? -44 * size : MINI_HEIGHT + 6 / scale;
      }

      private function loadLayout() : void
      {
         if(this.m_LayoutLoaded || !this.m_StorageReady || this.m_Storage == null) return;
         this.m_LayoutLoaded = true;
         var document:String;
         try { document = String(this.m_Storage.load(LAYOUT_STORAGE_NAME)); }
         catch(loadError:Error) { return; }
         if(document == null || document.length == 0 || document == "undefined" || document == "null") return;

         this.m_OffsetX = this.mouseField(document, "x", 0);
         this.m_OffsetY = this.mouseField(document, "y", 0);
         var savedScale:Number = this.mouseField(document, "scale", 0);
         var savedOpacity:Number = this.mouseField(document, "opacity", 0);
         this.m_ScaleOverride = savedScale >= 50 && savedScale <= 200 ? savedScale : 0;
         this.m_OpacityOverride = savedOpacity >= 20 && savedOpacity <= 100 ? savedOpacity : 0;
         this.refreshCardLook();
         if(this.m_MiniPlayer != null && this.m_MiniPlayer.visible) this.applyMiniOffset();
      }

      private function saveLayout() : void
      {
         if(!this.m_StorageReady || this.m_Storage == null) return;
         var document:String = "{\"x\":" + Math.round(this.m_OffsetX) + ",\"y\":" + Math.round(this.m_OffsetY);
         if(this.m_ScaleOverride > 0) document += ",\"scale\":" + Math.round(this.m_ScaleOverride);
         if(this.m_OpacityOverride > 0) document += ",\"opacity\":" + Math.round(this.m_OpacityOverride);
         try { this.m_Storage.save(LAYOUT_STORAGE_NAME, document + "}"); }
         catch(saveError:Error) {}
      }

      private function ensureStorage() : void
      {
         if(this.m_StorageReady) return;
         try
         {
            var code:Object = this.resolveBridge();
            if(code == null || code.modStorage == null) return;
            var storage:Object = code.modStorage;
            if(storage.register(STORAGE_NAME) === true)
            {
               this.m_Storage = storage;
               this.m_StorageReady = true;
            }
         }
         catch(storageError:Error)
         {
            this.m_Storage = null;
            this.m_StorageReady = false;
         }
      }

      private function resolveBridge() : Object
      {
         var rootObject:Object = this.memberOf(this, "root");
         var code:Object = this.memberOf(rootObject, "__SFCodeObj");
         if(code != null) return code;
         code = this.memberOf(this, "__SFCodeObj");
         if(code != null) return code;
         code = this.memberOf(this, "root1.__SFCodeObj");
         if(code != null) return code;
         code = this.memberOf(rootObject, "root1.__SFCodeObj");
         if(code != null) return code;

         var node:Object = this;
         var depth:uint = 0;
         while(node != null && depth < 32)
         {
            var found:Object = this.memberOf(node, "__SFCodeObj");
            if(found != null) return found;
            node = this.memberOf(node, "parent");
            depth++;
         }
         return null;
      }

      private function memberOf(param1:Object, param2:String) : Object
      {
         if(param1 == null || param2 == null || param2.length == 0) return null;
         try { return param1[param2]; } catch(error:Error) {}
         return null;
      }
   }
}
