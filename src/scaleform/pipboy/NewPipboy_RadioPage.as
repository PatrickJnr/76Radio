package
{
   import Shared.AS3.BSScrollingList;
   import Shared.AS3.Data.BSUIDataManager;
   import Shared.AS3.Data.FromClientDataEvent;
   import Shared.AS3.Events.CustomEvent;
   import Shared.GlobalFunc;
   import flash.display.Bitmap;
   import flash.display.BitmapData;
   import flash.display.DisplayObject;
   import flash.display.DisplayObjectContainer;
   import flash.display.Graphics;
   import flash.display.Loader;
   import flash.display.MovieClip;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.events.IOErrorEvent;
   import flash.events.MouseEvent;
   import flash.events.SecurityErrorEvent;
   import flash.events.TimerEvent;
   import flash.geom.Rectangle;
   import flash.net.SharedObject;
   import flash.system.ApplicationDomain;
   import flash.text.Font;
   import flash.net.URLLoader;
   import flash.net.URLRequest;
   import flash.text.TextField;
   import flash.text.TextFormat;
   import flash.utils.ByteArray;
   import flash.utils.Endian;
   import flash.utils.Timer;
   import flash.utils.getDefinitionByName;

   [SWF(width="876", height="700", backgroundColor="#333333", frameRate="30")]
   public class NewPipboy_RadioPage extends IPipBoyPage
   {
      private static const SPOTIFY_FREQUENCY:Number = 0.5;
      private static const SEPARATOR_FREQUENCY:Number = 0.25;
      private static const SPOTIFY_STATION_NAME:String = "Spotify Radio";
      private static const PANEL_FONT:String = "$MAIN_Font";
      private static const PANEL_FONT_BOLD:String = "$MAIN_Font_Bold";
      private static const PANEL_COLOR:uint = 16777215;
      private static const PANEL_DISABLED_COLOR:uint = 0x555555;
      private static const POLL_INTERVAL:uint = 250;
      private static const RELAY_BUILD:String = "0.1.0";
      private static const THEME_STORAGE_NAME:String = "r76radio.theme";

      private static const DIAG_STORAGE_NAME:String = "r76radio.diag";
      private static const PANEL_PROGRESS_WIDTH:uint = 250;

      private static const CJK_FONT_PATHS:Array = [
         "fonts_ja.swf",
         "../interface/fonts_ja.swf",
         "interface/fonts_ja.swf",
         "../Data/interface/fonts_ja.swf"
      ];

      private static const ART_NAMES:Array = ["r76radio.art.pixels"];

      private static const FILE_STATE_PATHS:Array = [
         "../modsdata/76radio/r76radio.state.json",
         "../Data/modsdata/76radio/r76radio.state.json",
         "../../Data/modsdata/76radio/r76radio.state.json",
         "modsdata/76radio/r76radio.state.json"
      ];

      private static const RELAY_PATHS:Array = [
         "../modsdata/r76radio.relay.json",
         "../Data/modsdata/r76radio.relay.json",
         "../../Data/modsdata/r76radio.relay.json",
         "modsdata/r76radio.relay.json"
      ];

      private static const CUSTOM_STATIONS_PATHS:Array = [
         "../modsdata/76radio/stations.json",
         "../Data/modsdata/76radio/stations.json",
         "../../Data/modsdata/76radio/stations.json",
         "modsdata/76radio/stations.json",
         "../modsdata/r76radio.stations.json",
         "../Data/modsdata/r76radio.stations.json",
         "../../Data/modsdata/r76radio.stations.json",
         "modsdata/r76radio.stations.json"
      ];

      private static const DEFAULT_ART_BASES:Array = [
         "../modsdata/76radio/",
         "../Data/modsdata/76radio/",
         "../../Data/modsdata/76radio/",
         "modsdata/76radio/"
      ];

      public var List_mc:BSScrollingList;
      public var DeadWave_mc:MovieClip;
      public var RadioWave_mc:RadioWave;

      private const ANIM_TIME:uint = 41;

      private var m_Timer:Timer;
      private var m_PollTimer:Timer;
      private var m_SpotifyEntry:Object;
      private var m_SpotifySelected:Boolean = false;
      private var m_SpotifyPlaying:Boolean = false;

      private var m_StreamActive:Boolean = false;

      private var m_StreamStation:String = "";
      private var m_AudioPlaying:Boolean = false;
      private var m_CanNext:Boolean = false;
      private var m_CanPrev:Boolean = false;
      private var m_CanToggle:Boolean = false;
      private var m_CanSeek:Boolean = false;
      private var m_PlaybackState:String = "unavailable";
      private var m_PositionMs:Number = 0;
      private var m_DurationMs:Number = 0;
      private var m_CapturedMs:Number = 0;

      private var m_CustomStations:Array = [];
      private var m_CustomStationsLoading:Boolean = false;
      private var m_CustomStationsAttempt:uint = 0;
      private var m_CustomStationsLoader:URLLoader;

      private var m_FileLoader:URLLoader;
      private var m_FileAttempt:uint = 0;
      private var m_FileLoading:Boolean = false;
      private var m_FileFields:Object;
      private var m_FileGoodPath:String = "";
      private var m_FileLoadedAt:int = 0;
      private var m_FileLoadTicks:uint = 0;
      private var m_FileReloadTicks:uint = 0;
      private var m_FileReloadErrors:uint = 0;
      private var m_FileRetryTicks:uint = 0;
      private var m_FileReload:URLLoader;

      private var m_ArtImage:DisplayObject;
      private var m_ArtLoader:URLLoader;
      private var m_ArtLoading:Boolean = false;
      private var m_ArtAttempt:uint = 0;
      private var m_ArtLoadTicks:uint = 0;
      private var m_ArtSignature:String = "";
      private var m_ArtLoadingSignature:String = "";
      private var m_ArtPixelWidth:int = 0;
      private var m_ArtPixelHeight:int = 0;
      private var m_ArtPixels:ByteArray;

      private var m_HudTools:Object;
      private var m_Storage:Object;
      private var m_StorageReady:Boolean = false;
      private var m_CommandStamp:Number = 0;
      private var m_MessageIteration:uint = 0;
      private var m_SessionToken:String = "";
      private var m_LastCommand:String = "";
      private var m_LastCommandAt:Number = 0;

      private var m_LastDiagnostic:String = "";
      private var m_LastDiagnosticAt:Number = 0;
      private var m_RelayLoader:URLLoader;
      private var m_RelayLoading:Boolean = false;
      private var m_RelayPresent:Boolean = false;
      private var m_RelayAttempt:uint = 0;
      private var m_RelayRetryTicks:uint = 0;
      private var m_Diagnostic:String;
      private var m_LastActiveState:Boolean = false;
      private var m_FrameTicks:uint = 0;
      private var m_LabelFont:String;
      private var m_LabelEmbed:Boolean = false;
      private var m_Labels:Array = [];
      private var m_PanelVisible:Boolean = true;
      private var m_ThemeTicks:uint = 0;
      private var m_ThemePublished:String = "";
      private var m_ThemeDocument:String = "";
      private var m_ThemeSubscribed:Boolean = false;
      private var m_ThemeLogged:Boolean = false;
      private var m_ThemeSource:String = "none";
      private var m_ThemeFiltersSeen:uint = 0;
      private var m_ThemeTransformsSeen:uint = 0;
      private var m_PipboyTintCss:String = "";
      private var m_ShowStatus:Boolean = false;
      private var m_ShowArtStatus:Boolean = false;
      private var m_ShowArtwork:Boolean = true;
      private var m_MarqueeTick:uint = 0;
      private var m_MarqueeHold:uint = 0;
      private var m_TitleRemeasure:Boolean = false;
      private var m_TitleClip:Sprite;
      private var m_TitleMask:Sprite;
      private var m_CjkFontLoader:Loader;
      private var m_CjkFontLoading:Boolean = false;
      private var m_CjkFontReady:Boolean = false;
      private var m_CjkFontRequested:Boolean = false;
      private var m_CjkFontAttempt:uint = 0;
      private var m_CjkFontTicks:uint = 0;
      private var m_CjkFontName:String = "";

      private var m_NowPlayingField:TextField;
      private var m_ArtistField:TextField;
      private var m_AlbumField:TextField;
      private var m_TimeField:TextField;
      private var m_ProgressBg:Sprite;
      private var m_ProgressFill:Sprite;
      private var m_ProgressKnob:Sprite;
      private var m_ProgressHit:Sprite;
      private var m_ProgressHover:Boolean = false;
      private var m_SeekPendingMs:Number = -1;
      private var m_SeekRequestedAt:Number = 0;
      private var m_SeekPendingUntil:Number = 0;
      private var m_TitleField:TextField;
      private var m_StateField:TextField;
      private var m_StatusField:TextField;
      private var m_ArtStatusField:TextField;
      private var m_PreviousButton:TextField;
      private var m_PlayButton:TextField;
      private var m_NextButton:TextField;

      public function NewPipboy_RadioPage()
      {
         super();
         this.List_mc.listEntryClass_Inspectable = "RadioListEntry";
         this.List_mc.numListItems_Inspectable = 9;
         this.List_mc.restoreListIndex_Inspectable = false;
         this.List_mc.enableScrollWrap = true;
         this.List_mc.addEventListener(BSScrollingList.SELECTION_CHANGE,this.onListSelectionChange);
         this.List_mc.addEventListener(BSScrollingList.ITEM_PRESS,this.onRadioItemPress);
         this.m_Timer = new Timer(this.ANIM_TIME);
         this.m_Timer.addEventListener(TimerEvent.TIMER,this.AnimateWave);
         this.m_Timer.start();
         this.m_PollTimer = new Timer(POLL_INTERVAL);
         this.m_PollTimer.addEventListener(TimerEvent.TIMER,this.onPoll);
         this.m_PollTimer.start();
         this.addEventListener(Event.ENTER_FRAME,this.onFrameTick);
         this.addEventListener(MouseEvent.CLICK,this.onPageClick);
         this.createSpotifyPanel();
         this.loadCustomStations();
         this.m_SessionToken = new Date().getTime().toString(16);
         m_EventPrefix = "RADIO::";
      }

      override public function onAddedToStage() : void
      {
         super.onAddedToStage();
         this.ensureStorage();
         this.ensureHudTools();
         this.subscribeThemeSource();
      }

      private function loadCustomStations() : void
      {
         if(this.m_CustomStationsLoading) return;
         if(this.m_CustomStationsAttempt >= CUSTOM_STATIONS_PATHS.length)
         {
            this.m_CustomStationsAttempt = 0;
            return;
         }

         this.m_CustomStationsLoading = true;
         this.m_CustomStationsLoader = new URLLoader();
         this.m_CustomStationsLoader.addEventListener(Event.COMPLETE, this.onCustomStationsLoaded);
         this.m_CustomStationsLoader.addEventListener(IOErrorEvent.IO_ERROR, this.onCustomStationsError);
         this.m_CustomStationsLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR, this.onCustomStationsError);

         try { this.m_CustomStationsLoader.load(new URLRequest(CUSTOM_STATIONS_PATHS[this.m_CustomStationsAttempt])); }
         catch(e:Error) { this.onCustomStationsError(new IOErrorEvent(IOErrorEvent.IO_ERROR)); }
      }

      private function onCustomStationsLoaded(param1:Event) : void
      {
         this.m_CustomStationsLoading = false;
         var document:String = "";
         try { document = String(this.m_CustomStationsLoader.data); } catch(e:Error) { document = ""; }

         var startIdx:int = document.indexOf("\"stations\"");
         if(startIdx >= 0)
         {
            var arrayStart:int = document.indexOf("[", startIdx);
            var arrayEnd:int = document.lastIndexOf("]");
            if(arrayStart >= 0 && arrayEnd > arrayStart)
            {
               var arrayText:String = document.substring(arrayStart + 1, arrayEnd);
               var objects:Array = arrayText.split("}");

               var fakeFreq:Number = -1.0;
               for(var i:int = 0; i < objects.length; i++)
               {
                  var objText:String = objects[i];

                  var stationUrl:String = this.readJsonString(objText,"streamurl");

                  if(stationUrl.length > 0)
                  {
                     var stationName:String = this.readJsonString(objText,"name");
                     if(stationName.length == 0) stationName = stationUrl;

                     this.m_CustomStations.push({
                        "StationName": stationName,
                        "Frequency": fakeFreq,
                        "InRange": true,
                        "Active": false,
                        "CustomUrl": stationUrl
                     });
                     fakeFreq -= 1.0;
                  }
               }
            }
         }

         if(this.List_mc != null && this.List_mc.entryList != null)
         {
            this.processProvider(PageData);
         }
      }

      private function onCustomStationsError(param1:Event) : void
      {
         this.m_CustomStationsLoading = false;
         this.m_CustomStationsAttempt++;
         this.loadCustomStations();
      }

      private function readJsonString(param1:String, param2:String) : String
      {
         var key:int = param1.indexOf("\"" + param2 + "\"");
         if(key < 0) return "";
         var colon:int = param1.indexOf(":",key + param2.length + 2);
         if(colon < 0) return "";
         var quote:int = param1.indexOf("\"",colon + 1);
         if(quote < 0) return "";
         var end:int = param1.indexOf("\"",quote + 1);
         if(end < 0) return "";
         return param1.substring(quote + 1,end);
      }

      private function subscribeThemeSource() : void
      {
         if(this.m_ThemeSubscribed) return;
         try
         {
            BSUIDataManager.Subscribe("HUDColors",this.onPipboyColors);
            this.m_ThemeSubscribed = true;
         }
         catch(colorsError:Error) {}
      }

      private function ensureStorage() : void
      {
         if(this.m_StorageReady) return;
         try
         {
            var code:Object = this.resolveBridge();
            if(code == null) return;
            var storage:Object = this.memberOf(code,"modStorage");
            if(storage == null) return;
            if(storage.register("r76radio.command") === true)
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
         var rootObject:Object = this.memberOf(this,"root");
         var code:Object = this.bridgeFrom(rootObject);
         if(code != null) return code;
         code = this.bridgeFrom(this);
         if(code != null) return code;
         var root1:Object = this.memberOf(rootObject,"root1");
         code = this.bridgeFrom(root1);
         if(code != null) return code;
         code = this.bridgeFrom(this.memberOf(root1,"BaseInstance"));
         if(code != null) return code;
         root1 = this.memberOf(this,"root1");
         code = this.bridgeFrom(root1);
         if(code != null) return code;
         code = this.bridgeFrom(this.memberOf(root1,"BaseInstance"));
         if(code != null) return code;
         var node:Object = this;
         var depth:uint = 0;
         while(node != null && depth < 64)
         {
            code = this.bridgeFrom(node);
            if(code != null) return code;
            node = this.memberOf(node,"parent");
            depth++;
         }
         return null;
      }

      private function bridgeFrom(param1:Object) : Object
      {
         if(param1 == null) return null;
         var code:Object = this.memberOf(param1,"__SFCodeObj");
         if(code != null && this.memberOf(code,"modStorage") != null) return code;
         code = this.memberOf(this.memberOf(param1,"BaseInstance"),"__SFCodeObj");
         if(code != null && this.memberOf(code,"modStorage") != null) return code;
         return null;
      }

      private function memberOf(param1:Object, param2:String) : Object
      {
         if(param1 == null || param2 == null) return null;
         try { return param1[param2]; } catch(error:Error) {}
         return null;
      }

      private function ensureHudTools() : void
      {
         if(this.m_HudTools != null) return;
         try
         {
            var toolsClass:Class = getDefinitionByName("SharedHUDTools") as Class;
            if(toolsClass != null) this.m_HudTools = new toolsClass("R76Radio_Pipboy","Pipboy");
         }
         catch(toolsError:Error) {}
      }

      override public function processProvider(param1:Object, param2:uint = 0) : void
      {
         PageData = param1;
         if(this.List_mc == null) return;
         var supplied:Array = PageData.StationsA != null ? PageData.StationsA.slice() : [];

         var vanilla:Array = [];
         this.m_SpotifyEntry = null;
         var index:int = 0;
         while(index < supplied.length)
         {
            var suppliedEntry:Object = supplied[index];
            if(suppliedEntry != null)
            {
               if(this.isSpotifyEntry(suppliedEntry)) this.m_SpotifyEntry = suppliedEntry;
               else vanilla.push(suppliedEntry);
            }
            index++;
         }
         if(this.m_SpotifyEntry == null) this.m_SpotifyEntry = this.createSpotifyEntry();

         var custom:Array = [];
         for(var i:int = 0; i < this.m_CustomStations.length; i++)
         {
            custom.push(this.m_CustomStations[i]);
         }

         vanilla.sortOn(["InRange","StationName"],[Array.DESCENDING | Array.NUMERIC,Array.CASEINSENSITIVE]);
         custom.sortOn("StationName",Array.CASEINSENSITIVE);
         var separator:Object = this.createSeparatorEntry();
         var stations:Array = vanilla.concat([separator],[this.m_SpotifyEntry],custom);

         this.syncEntryState();
         this.List_mc.entryList = stations;
         this.List_mc.InvalidateData();
         if(this.stage != null) this.stage.focus = this.List_mc;
         if(this.List_mc.selectedIndex == -1) this.List_mc.selectedIndex = 0;
         var entry:Object = this.List_mc.selectedEntry;
         this.m_SpotifySelected = Boolean(entry) && this.isSpotifyEntry(entry);
         this.setPanelVisible(this.m_SpotifySelected || this.m_StreamActive);
         this.updateRadioFrequency();
         this.safeRefresh();
      }

      override public function SetPlatform(param1:uint, param2:Boolean, param3:uint, param4:uint) : void
      {
         if(this.List_mc != null) this.List_mc.SetPlatform(param1,param2,param3,param4);
      }

      override public function ProcessUserEvent(param1:String) : Boolean
      {
         if(this.m_SpotifySelected)
         {
            switch(param1)
            {
               case "Accept":
                  this.onPlayClick(null);
                  return true;
               case "Cancel":
                  break;
            }
         }
         switch(param1)
         {
            case "Accept":
               this.onRadioItemPress(null);
         }
         return false;
      }

      private function onRadioItemPress(param1:Event) : void
      {
         var entry:Object = this.List_mc.selectedEntry;

         if(Boolean(entry) && Boolean(entry.Separator))
         {
            GlobalFunc.PlayMenuSound("UIMenuCancel");
            return;
         }

         if(Boolean(entry) && this.isSpotifyEntry(entry))
         {
            var stoppedStation:Boolean = this.stopActiveStation();
            this.sendHudMessage("showcard");

            if(!(this.m_SpotifyPlaying && stoppedStation)) this.sendCommand("toggle");
            return;
         }

         if(this.m_AudioPlaying) this.sendCommand("pause");

         if(Boolean(entry) && Boolean(entry.InRange))
         {
            if(entry.CustomUrl != null)
            {

               this.stopActiveStation();

               this.sendDiagnostic("station " + entry.StationName);
               this.sendCommand("stream|" + entry.CustomUrl);
               this.m_StreamActive = true;
               this.m_StreamStation = entry.StationName;
               this.m_AudioPlaying = true;
               this.m_SpotifyPlaying = false;
               this.syncEntryState();
               if(this.List_mc != null) this.List_mc.InvalidateData();
               this.sendHudMessage("showcard");
               this.setLabelText(this.m_StateField, "STREAMING");
               this.setLabelText(this.m_NowPlayingField, "CUSTOM RADIO");
               this.setTitleText(entry.StationName);
               this.setLabelText(this.m_ArtistField, "Internet Stream");
               this.setLabelText(this.m_AlbumField, "Live");
               this.m_DurationMs = 0;
               this.updateTimeField();
            }
            else
            {

               this.sendHudMessage("hidecard");
               BSUIDataManager.dispatchEvent(new CustomEvent(NewPipBoyShared.RADIO_TOGGLE_ACTIVE,{"frequency":entry.Frequency}));
            }
         }
         else
         {
            GlobalFunc.PlayMenuSound("UIMenuCancel");
         }
      }

      private function stopActiveStation() : Boolean
      {
         if(this.List_mc == null) return false;
         var stations:Array = this.List_mc.entryList;
         if(stations == null) return false;
         var index:uint = 0;
         while(index < stations.length)
         {
            var station:Object = stations[index];

            if(Boolean(station) && !this.isSpotifyEntry(station) && station.CustomUrl == null && Boolean(station.Active))
            {
               BSUIDataManager.dispatchEvent(new CustomEvent(NewPipBoyShared.RADIO_TOGGLE_ACTIVE,{"frequency":station.Frequency}));
               return true;
            }
            index++;
         }
         return false;
      }

      private function onListSelectionChange(param1:Event) : void
      {
         var entry:Object = this.List_mc.selectedEntry;
         this.m_SpotifySelected = Boolean(entry) && this.isSpotifyEntry(entry);
         this.setPanelVisible(this.m_SpotifySelected || this.m_StreamActive);
         if(Boolean(entry) && Boolean(entry.Separator))
         {

            GlobalFunc.PlayMenuSound(GlobalFunc.MENU_SOUND_FOCUS_CHANGE);
            return;
         }
         var showingArt:Boolean = this.m_ArtImage != null && this.m_PanelVisible;

         if(Boolean(entry))
         {
            if(this.DeadWave_mc != null) this.DeadWave_mc.visible = false;
            if(this.RadioWave_mc != null) this.RadioWave_mc.visible = !showingArt;
            GlobalFunc.PlayMenuSound(GlobalFunc.MENU_SOUND_FOCUS_CHANGE);
         }
         else
         {
            if(this.DeadWave_mc != null) this.DeadWave_mc.visible = true;
            if(this.RadioWave_mc != null) this.RadioWave_mc.visible = false;
         }

         if(!this.m_SpotifySelected && Boolean(entry))
         {
            if(entry.CustomUrl != null)
            {

               if(this.RadioWave_mc != null) {
                  this.RadioWave_mc.freq = RadioWave.NO_AMP;
                  this.RadioWave_mc.InitWave();
               }
            }
            else
            {
               BSUIDataManager.dispatchEvent(new CustomEvent(NewPipBoyShared.RADIO_HIGHLIGHT,{"frequency":entry.Frequency}));
               this.updateRadioFrequency();
            }
         }
      }

      private function updateRadioFrequency() : void
      {
         var entry:Object = this.List_mc != null ? this.List_mc.selectedEntry : null;
         if(Boolean(entry) && this.RadioWave_mc != null)
         {
            if(this.isSpotifyEntry(entry))
               this.RadioWave_mc.freq = this.m_SpotifyPlaying ? int(GlobalFunc.Lerp(5,10,70,110,76,true)) : RadioWave.NO_AMP;
            else if(Boolean(entry.Separator))
               this.RadioWave_mc.freq = RadioWave.NO_AMP;
            else if(entry.CustomUrl == null)
               this.RadioWave_mc.freq = entry.InRange ? int(GlobalFunc.Lerp(5,10,70,110,entry.Frequency,true)) : RadioWave.NO_AMP;
            else
               this.RadioWave_mc.freq = RadioWave.NO_AMP;

            this.RadioWave_mc.InitWave();
         }
      }

      private function AnimateWave(param1:TimerEvent) : void
      {
         if(this.RadioWave_mc != null) this.RadioWave_mc.Animate();
      }

      private function onPoll(param1:TimerEvent) : void
      {
         this.safeRefresh();
         this.m_ThemeTicks++;
         if(this.m_ThemeTicks % 8 == 0) this.publishPipboyTheme();
      }

      private function publishPipboyTheme() : void
      {
         this.subscribeThemeSource();
         var color:String = this.readPipboyTint();
         if(!this.m_ThemeLogged)
         {
            this.m_ThemeLogged = true;
            this.sendDiagnostic("theme " + this.m_ThemeSource + " f" + this.m_ThemeFiltersSeen + " t" + this.m_ThemeTransformsSeen + (color.length > 0 ? " " + color : " unreadable"));
         }
         if(!this.m_StorageReady || this.m_Storage == null) return;
         if(color.length == 0 && this.m_ThemeDocument.length > 0) return;
         var document:String = "{\"color\":\"" + color + "\",\"source\":\"" + this.m_ThemeSource + "\"}";
         if(document == this.m_ThemeDocument) return;
         try
         {
            if(this.m_Storage.save(THEME_STORAGE_NAME,document) === true)
            {
               this.m_ThemeDocument = document;
               this.m_ThemePublished = color;
            }
         }
         catch(themeError:Error) {}
      }

      private function onPipboyColors(param1:FromClientDataEvent) : void
      {
         var colors:Object = param1 != null ? param1.data : null;
         if(colors == null) return;
         var hue:Number = Number(colors.hue);
         var saturation:Number = Number(colors.saturation);
         var brightness:Number = Number(colors.value);
         var contrast:Number = Number(colors.contrast);
         if(hue == 0 && saturation == 0 && brightness == 0 && contrast == 0) return;
         var color:String = this.computePipboyTint(hue,saturation,brightness,contrast);
         if(color.length == 0) return;
         this.m_PipboyTintCss = color;
         this.publishPipboyTheme();
      }

      private function computePipboyTint(hue:Number, saturation:Number, brightness:Number, contrast:Number) : String
      {
         if(isNaN(hue) || isNaN(saturation) || isNaN(brightness) || isNaN(contrast)) return "";
         try
         {
            var adjustClass:Class = ApplicationDomain.currentDomain.getDefinition("fl.motion.AdjustColor") as Class;
            if(adjustClass == null) return "";
            var adjust:Object = new adjustClass();
            adjust.hue = hue != 0 ? hue : 0.001;
            adjust.saturation = saturation;
            adjust.brightness = brightness != 0 ? brightness : 0.001;
            adjust.contrast = contrast;
            var matrix:Array = adjust.CalculateFinalFlatArray() as Array;
            if(matrix == null || matrix.length < 20) return "";
            var red:Number = Number(matrix[0]) * 255 + Number(matrix[1]) * 255 + Number(matrix[2]) * 255 + Number(matrix[3]) * 255 + Number(matrix[4]);
            var green:Number = Number(matrix[5]) * 255 + Number(matrix[6]) * 255 + Number(matrix[7]) * 255 + Number(matrix[8]) * 255 + Number(matrix[9]);
            var blue:Number = Number(matrix[10]) * 255 + Number(matrix[11]) * 255 + Number(matrix[12]) * 255 + Number(matrix[13]) * 255 + Number(matrix[14]);
            if(isNaN(red) || isNaN(green) || isNaN(blue)) return "";
            var redByte:int = Math.max(0,Math.min(255,Math.round(red)));
            var greenByte:int = Math.max(0,Math.min(255,Math.round(green)));
            var blueByte:int = Math.max(0,Math.min(255,Math.round(blue)));
            if(redByte + greenByte + blueByte < 90) return "";
            return this.toHexColor(redByte,greenByte,blueByte);
         }
         catch(colorError:Error) {}
         return "";
      }

      private function readPipboyTint() : String
      {
         var node:DisplayObject = this;
         var depth:uint = 0;
         this.m_ThemeSource = "none";
         this.m_ThemeFiltersSeen = 0;
         this.m_ThemeTransformsSeen = 0;
         while(node != null && depth < 16)
         {
            var transformed:String = this.readColorTransformTint(node);
            if(transformed.length > 0)
            {
               this.m_ThemeSource = "transform";
               this.m_ThemeTransformsSeen++;
               return transformed;
            }
            var matrix:Object = this.findColorMatrix(node);
            if(matrix != null && this.isColorMatrixTint(matrix))
            {
               var red:Number = Number(matrix[0]) * 255 + Number(matrix[1]) * 255 + Number(matrix[2]) * 255 + Number(matrix[3]) * 255 + Number(matrix[4]);
               var green:Number = Number(matrix[5]) * 255 + Number(matrix[6]) * 255 + Number(matrix[7]) * 255 + Number(matrix[8]) * 255 + Number(matrix[9]);
               var blue:Number = Number(matrix[10]) * 255 + Number(matrix[11]) * 255 + Number(matrix[12]) * 255 + Number(matrix[13]) * 255 + Number(matrix[14]);
               if(!isNaN(red) && !isNaN(green) && !isNaN(blue))
               {
                  var redByte:int = Math.max(0,Math.min(255,Math.round(red)));
                  var greenByte:int = Math.max(0,Math.min(255,Math.round(green)));
                  var blueByte:int = Math.max(0,Math.min(255,Math.round(blue)));
                  if(redByte + greenByte + blueByte >= 90)
                  {
                     this.m_ThemeSource = "filter";
                     return this.toHexColor(redByte,greenByte,blueByte);
                  }
               }
            }
            node = node.parent;
            depth++;
         }
         if(this.m_PipboyTintCss.length > 0) this.m_ThemeSource = "hud";
         return this.m_PipboyTintCss;
      }

      private function readColorTransformTint(param1:DisplayObject) : String
      {
         var transform:Object = null;
         try { transform = param1.transform.colorTransform; } catch(transformError:Error) { return ""; }
         if(transform == null) return "";
         var redMultiplier:Number = Number(transform.redMultiplier);
         var greenMultiplier:Number = Number(transform.greenMultiplier);
         var blueMultiplier:Number = Number(transform.blueMultiplier);
         var redOffset:Number = Number(transform.redOffset);
         var greenOffset:Number = Number(transform.greenOffset);
         var blueOffset:Number = Number(transform.blueOffset);
         if(isNaN(redMultiplier) || isNaN(greenMultiplier) || isNaN(blueMultiplier) || isNaN(redOffset) || isNaN(greenOffset) || isNaN(blueOffset)) return "";
         if(Math.abs(redMultiplier - 1) < 0.005 && Math.abs(greenMultiplier - 1) < 0.005 && Math.abs(blueMultiplier - 1) < 0.005 && Math.abs(redOffset) < 0.5 && Math.abs(greenOffset) < 0.5 && Math.abs(blueOffset) < 0.5) return "";
         var red:int = Math.max(0,Math.min(255,Math.round(255 * redMultiplier + redOffset)));
         var green:int = Math.max(0,Math.min(255,Math.round(255 * greenMultiplier + greenOffset)));
         var blue:int = Math.max(0,Math.min(255,Math.round(255 * blueMultiplier + blueOffset)));
         if(red + green + blue < 30) return "";
         return this.toHexColor(red,green,blue);
      }

      private function isColorMatrixTint(param1:Object) : Boolean
      {
         var identity:Array = [1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0];
         var index:uint = 0;
         while(index < 20)
         {
            var actual:Number = Number(param1[index]);
            if(isNaN(actual) || Math.abs(actual - Number(identity[index])) > 0.01) return true;
            index++;
         }
         return false;
      }

      private function findColorMatrix(param1:DisplayObject) : Object
      {
         var list:Array = null;
         try { list = param1.filters; } catch(filterError:Error) { return null; }
         if(list == null) return null;
         if(list.length > 0) this.m_ThemeFiltersSeen++;
         var index:uint = 0;
         while(index < list.length)
         {
            var matrix:Object = null;
            try { matrix = list[index].matrix; } catch(matrixError:Error) { matrix = null; }
            if(matrix != null)
            {
               try
               {
                  if(matrix.length >= 20) return matrix;
               }
               catch(lengthError:Error) {}
            }
            index++;
         }
         return null;
      }

      private function toHexColor(param1:int, param2:int, param3:int) : String
      {
         var value:String = ((param1 << 16) | (param2 << 8) | param3).toString(16).toUpperCase();
         while(value.length < 6) value = "0" + value;
         return value;
      }

      private function onFrameTick(param1:Event) : void
      {
         this.m_FrameTicks++;
         if(this.m_TitleRemeasure)
         {
            this.m_TitleRemeasure = false;
            this.fitTitleField();
         }
         this.updateTitleMarquee();
         if(this.m_FrameTicks % 15 != 0) return;
         this.safeRefresh();
      }

      private function updateTitleMarquee() : void
      {
         if(this.m_TitleField == null || this.m_TitleClip == null || !this.m_PanelVisible) return;
         var contentWidth:Number = this.m_TitleField.textWidth;
         if(this.m_TitleField.width - 6 > contentWidth) contentWidth = this.m_TitleField.width - 6;
         var travel:Number = contentWidth - 250;
         if(travel <= 0)
         {
            this.m_TitleField.x = 0;
            this.m_MarqueeTick = 0;
            this.m_MarqueeHold = 0;
            return;
         }
         this.m_MarqueeTick++;
         if(this.m_MarqueeTick <= 12) return;

         if(this.m_MarqueeHold > 0)
         {
            this.m_MarqueeHold--;
            if(this.m_MarqueeHold == 0)
            {
               this.m_TitleField.x = 0;
               this.m_MarqueeTick = 0;
            }
            return;
         }

         var offset:Number = -this.m_TitleField.x;
         if(offset >= travel)
         {
            this.m_TitleField.x = -travel;
            this.m_MarqueeHold = 60;
            return;
         }
         this.m_TitleField.x = -(offset + 1);
      }

      private function fitTitleField() : void
      {
         if(this.m_TitleField == null) return;
         this.m_MarqueeTick = 0;
         this.m_MarqueeHold = 0;
         this.m_TitleField.x = 0;
         this.m_TitleField.wordWrap = false;
         this.m_TitleField.multiline = false;
         var needed:Number = this.m_TitleField.textWidth + 6;
         if(needed < 250) needed = 250;
         this.m_TitleField.width = needed;
      }

      private function setTitleText(param1:String) : void
      {
         if(this.m_TitleField == null) return;
         if(this.m_TitleField.text == param1) return;
         this.setLabelText(this.m_TitleField,param1);
         this.fitTitleField();
         this.m_TitleRemeasure = this.m_TitleField.textWidth <= 0;
      }

      private function safeRefresh() : void
      {
         this.ensureStorage();
         this.ensureHudTools();
         this.pollRelayStatus();
         this.pollArtworkLoad();
         this.pollCjkFontLoad();
         try { this.refreshLabelFont(); } catch(fontError:Error) {}
         this.refreshSpotifyState();
      }

      private function pollArtworkLoad() : void
      {
         if(!this.m_ArtLoading) return;
         this.m_ArtLoadTicks++;
         if(this.m_ArtLoadTicks <= 40) return;
         this.abortArtworkLoad();
         this.m_ArtAttempt++;
         if(this.m_ArtAttempt < this.artworkPaths().length) this.loadArtwork();
         else this.hideArtwork();
      }

      private function refreshLabelFont() : void
      {
         if(this.m_LabelFont != null) return;
         var sample:TextField = this.findTextFieldIn(this.List_mc,0);
         if(sample == null) return;
         var format:TextFormat = sample.getTextFormat();
         if(format == null || format.font == null || format.font.length == 0) return;
         if(this.m_LabelFont == format.font && this.m_LabelEmbed == sample.embedFonts) return;
         this.m_LabelFont = format.font;
         this.m_LabelEmbed = sample.embedFonts;
         this.applyLabelStyle();
      }

      private function findTextFieldIn(param1:DisplayObjectContainer, param2:uint) : TextField
      {
         if(param1 == null || param2 > 3) return null;
         var index:uint = 0;
         var limit:uint = uint(Math.min(param1.numChildren,16));
         while(index < limit)
         {
            var child:DisplayObject = param1.getChildAt(index);
            if(child is TextField && Boolean((child as TextField).text) && (child as TextField).text.length > 0) return child as TextField;
            if(child is DisplayObjectContainer)
            {
               var found:TextField = this.findTextFieldIn(child as DisplayObjectContainer,param2 + 1);
               if(found != null) return found;
            }
            index++;
         }
         return null;
      }

      private function applyLabelStyle() : void
      {
         var index:uint = 0;
         while(index < this.m_Labels.length)
         {
            this.applyLabelStyleTo(this.m_Labels[index]["field"] as TextField,uint(this.m_Labels[index]["size"]));
            index++;
         }
         this.fitTitleField();
         this.applyButtonState();
      }

      private function setPanelVisible(param1:Boolean) : void
      {
         if(this.m_PanelVisible == param1) return;
         this.m_PanelVisible = param1;
         var index:uint = 0;
         while(index < this.m_Labels.length)
         {
            var field:TextField = this.m_Labels[index]["field"] as TextField;
            if(field != null) field.visible = param1;
            index++;
         }
         if(this.m_TitleClip != null) this.m_TitleClip.visible = param1;
         if(this.m_ProgressBg != null) this.m_ProgressBg.visible = param1;
         if(this.m_ProgressFill != null) this.m_ProgressFill.visible = param1;
         if(this.m_ProgressKnob != null) this.m_ProgressKnob.visible = param1;
         if(this.m_ProgressHit != null) this.m_ProgressHit.visible = param1;
         if(!param1) this.setProgressHover(false);
         this.applyUiVisibility();
         if(!param1) this.hideArtwork();
         else if(!this.m_StreamActive && this.m_ShowArtwork && this.m_ArtImage == null && this.m_ArtSignature.length > 0)
         {
            this.m_ArtAttempt = 0;
            this.loadArtwork();
         }
      }

      private function applyUiVisibility() : void
      {
         if(this.m_StatusField != null) this.m_StatusField.visible = this.m_PanelVisible && this.m_ShowStatus;
         if(this.m_ArtStatusField != null) this.m_ArtStatusField.visible = this.m_PanelVisible && this.m_ShowArtStatus;
      }

      private function applyUiFlags(param1:Object) : void
      {
         var artworkEnabled:Boolean = param1["uiArtwork"] != "0";
         var artworkChanged:Boolean = this.m_ShowArtwork != artworkEnabled;
         this.m_ShowStatus = param1["uiStatus"] == "1";
         this.m_ShowArtStatus = param1["uiArtStatus"] == "1";
         this.m_ShowArtwork = artworkEnabled;
         this.applyUiVisibility();
         if(!this.m_ShowArtwork)
         {
            this.hideArtwork();
         }
         else if(artworkChanged && !this.m_StreamActive && this.m_PanelVisible && this.m_ArtImage == null && this.m_ArtSignature.length > 0)
         {
            this.m_ArtAttempt = 0;
            this.loadArtwork();
         }
      }

      private function onPageClick(param1:MouseEvent) : void
      {
         if(this.List_mc != null && this.stage != null && this.stage.focus != this.List_mc) this.stage.focus = this.List_mc;
      }

      private function applyLabelStyleTo(param1:TextField, param2:uint) : void
      {
         if(param1 == null) return;
         var format:TextFormat;
         if(this.hasCjk(param1.text))
         {
            if(this.m_CjkFontReady)
            {
               format = new TextFormat(this.m_CjkFontName,param2,PANEL_COLOR);
               param1.embedFonts = true;
            }
            else
            {
               format = new TextFormat("Yu Gothic",param2,PANEL_COLOR);
               param1.embedFonts = false;
               this.beginCjkFontLoad();
            }
         }
         else
         {
            format = new TextFormat(PANEL_FONT_BOLD,param2,PANEL_COLOR);
            param1.embedFonts = this.m_LabelEmbed;
         }
         param1.defaultTextFormat = format;
         param1.setTextFormat(format);
         param1.textColor = PANEL_COLOR;
      }

      private function hasCjk(param1:String) : Boolean
      {
         if(param1 == null) return false;
         var index:uint = 0;
         while(index < param1.length)
         {
            if(param1.charCodeAt(index) >= 0x2E80) return true;
            index++;
         }
         return false;
      }

      private function beginCjkFontLoad() : void
      {
         if(this.m_CjkFontReady || this.m_CjkFontLoading || this.m_CjkFontRequested) return;
         this.m_CjkFontRequested = true;
         this.m_CjkFontAttempt = 0;
         this.loadCjkFont();
      }

      private function loadCjkFont() : void
      {
         if(this.m_CjkFontReady || this.m_CjkFontLoading) return;
         if(this.m_CjkFontAttempt >= CJK_FONT_PATHS.length)
         {
            this.sendDiagnostic("cjk font unavailable");
            return;
         }
         this.m_CjkFontLoading = true;
         this.m_CjkFontTicks = 0;
         this.m_CjkFontLoader = new Loader();
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(Event.COMPLETE,this.onCjkFontComplete);
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,this.onCjkFontError);
         this.m_CjkFontLoader.contentLoaderInfo.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onCjkFontError);
         this.sendDiagnostic("cjk font load " + this.m_CjkFontAttempt);
         try { this.m_CjkFontLoader.load(new URLRequest(CJK_FONT_PATHS[this.m_CjkFontAttempt])); }
         catch(fontLoadError:Error) { this.onCjkFontError(new IOErrorEvent(IOErrorEvent.IO_ERROR)); }
      }

      private function onCjkFontComplete(param1:Event) : void
      {
         if(!this.m_CjkFontLoading) return;
         this.m_CjkFontLoading = false;
         this.m_CjkFontTicks = 0;
         try
         {
            var domain:ApplicationDomain = this.m_CjkFontLoader.contentLoaderInfo.applicationDomain;
            var fontClass:Class = domain.getDefinition("$MAIN_Font") as Class;
            if(fontClass == null) throw new Error("$MAIN_Font missing");
            Font.registerFont(fontClass);
            var font:Font = new fontClass();
            this.m_CjkFontName = font.fontName;
            this.m_CjkFontReady = true;
            this.sendDiagnostic("cjk font ready " + this.m_CjkFontName);
            this.applyLabelStyle();
         }
         catch(fontError:Error)
         {
            this.sendDiagnostic("cjk font decode err " + fontError.errorID);
            this.m_CjkFontLoader = null;
            this.m_CjkFontAttempt++;
            this.loadCjkFont();
         }
      }

      private function onCjkFontError(param1:Event) : void
      {
         if(!this.m_CjkFontLoading) return;
         this.m_CjkFontLoading = false;
         this.m_CjkFontTicks = 0;
         this.sendDiagnostic("cjk font error " + this.m_CjkFontAttempt + " " + param1.type);
         this.m_CjkFontLoader = null;
         this.m_CjkFontAttempt++;
         this.loadCjkFont();
      }

      private function pollCjkFontLoad() : void
      {
         if(!this.m_CjkFontLoading) return;
         this.m_CjkFontTicks++;
         if(this.m_CjkFontTicks <= 120) return;
         this.m_CjkFontLoading = false;
         this.m_CjkFontTicks = 0;
         this.sendDiagnostic("cjk font timeout " + this.m_CjkFontAttempt);
         this.m_CjkFontLoader = null;
         this.m_CjkFontAttempt++;
         this.loadCjkFont();
      }

      private function isSpotifyEntry(param1:Object) : Boolean
      {
         return param1 != null && param1.Frequency == SPOTIFY_FREQUENCY;
      }

      private function findSpotifyEntry(param1:Array) : Object
      {
         var index:uint = 0;
         while(index < param1.length)
         {
            if(this.isSpotifyEntry(param1[index])) return param1[index];
            index++;
         }
         return null;
      }

      private function createSpotifyEntry() : Object
      {
         return {"StationName":SPOTIFY_STATION_NAME,"Frequency":SPOTIFY_FREQUENCY,"InRange":true,"Active":false};
      }

      private function createSeparatorEntry() : Object
      {
         return {"StationName":"- - - - - - - - - -","Frequency":SEPARATOR_FREQUENCY,"InRange":true,"Active":false,"Separator":true};
      }

      private function syncEntryState() : Boolean
      {
         var changed:Boolean = false;
         if(this.m_SpotifyEntry != null)
         {
            if(Boolean(this.m_SpotifyEntry.Active) != this.m_SpotifyPlaying) changed = true;
            this.m_SpotifyEntry.Active = this.m_SpotifyPlaying;
            this.m_SpotifyEntry.InRange = true;
         }
         if(this.m_CustomStations != null)
         {
            var index:uint = 0;
            while(index < this.m_CustomStations.length)
            {
               var station:Object = this.m_CustomStations[index];
               if(station != null)
               {
                  var stationActive:Boolean = this.m_StreamActive && this.m_StreamStation.length > 0 &&
                     String(station.StationName) == this.m_StreamStation;
                  if(Boolean(station.Active) != stationActive) changed = true;
                  station.Active = stationActive;
               }
               index++;
            }
         }
         return changed;
      }

      private function parseFlatJson(param1:String) : Object
      {
         var fields:Object = {};
         var index:uint = 0;
         while(true)
         {
            var keyStart:int = param1.indexOf("\"",index);
            if(keyStart < 0) break;
            var keyEnd:int = param1.indexOf("\"",keyStart + 1);
            if(keyEnd < 0) break;
            var colon:int = param1.indexOf(":",keyEnd + 1);
            if(colon < 0) break;
            var cursor:uint = colon + 1;
            var value:String;
            if(param1.substr(cursor,1) == "\"")
            {
               var valueEnd:int = param1.indexOf("\"",cursor + 1);
               if(valueEnd < 0) break;
               value = param1.substring(cursor + 1,valueEnd);
               index = valueEnd + 1;
            }
            else
            {
               var tokenEnd:uint = cursor;
               while(tokenEnd < param1.length)
               {
                  var character:String = param1.substr(tokenEnd,1);
                  if(character == "," || character == "}") break;
                  tokenEnd++;
               }
               value = param1.substring(cursor,tokenEnd);
               index = tokenEnd;
            }
            fields[param1.substring(keyStart + 1,keyEnd)] = value;
            if(index >= param1.length) break;
         }
         return fields;
      }

      private function sendCommand(param1:String) : void
      {
         var now:Number = new Date().getTime();
         if(param1 == this.m_LastCommand && now - this.m_LastCommandAt < 250) return;
         this.m_LastCommand = param1;
         this.m_LastCommandAt = now;
         this.ensureStorage();
         if(this.writeStorageCommand(param1)) return;

         if(param1.substr(0,7) == "stream|")
         {
            if(this.sendHudMessage(this.encodeCommand(param1))) return;
         }
         else if(this.sendHudMessage(param1)) return;
         if(this.writeSharedCommand(param1)) return;

         this.sendDiagnostic("no route " + this.sanitize(param1));
         GlobalFunc.PlayMenuSound("UIMenuCancel");
      }

      private function sendHudMessage(param1:String) : Boolean
      {
         try
         {

            var command:String = this.sanitizeText(param1,900).split("|").join(" ");
            var token:String = this.m_SessionToken + "-" + (this.m_MessageIteration++).toString(16);
            var message:String = String.fromCharCode(8192,8192) + "R76Radio_Pipboy|" +
               (this.m_MessageIteration).toString(16) + "|R76Radio_Hud|MSG|" + command + "#" + token + "|true";
            GlobalFunc.ShowHUDMessage(message);
            return true;
         }
         catch(messageError:Error) { return false; }
      }

      private function sendDiagnostic(param1:String) : void
      {
         var now:Number = new Date().getTime();
         if(param1 == this.m_LastDiagnostic && now - this.m_LastDiagnosticAt < 30000) return;
         this.m_LastDiagnostic = param1;
         this.m_LastDiagnosticAt = now;
         try
         {
            var line:String = "diag:" + this.sanitize(param1);
            if(line.length > 60) line = line.substring(0,60);
            if(this.writeDiagnostic(line)) return;
            if(this.sendHudMessage(line)) return;
            this.writeSharedCommand(line);
         }
         catch(diagnosticError:Error) {}
      }

      private function writeDiagnostic(param1:String) : Boolean
      {
         if(!this.m_StorageReady || this.m_Storage == null) return false;
         try
         {
            var stamp:Number = new Date().getTime();
            if(stamp <= this.m_CommandStamp) stamp = this.m_CommandStamp + 1;
            this.m_CommandStamp = stamp;
            var document:String = "{\"stamp\":" + stamp + ",\"command\":\"" + this.sanitizeCommand(param1) + "\"}";
            return this.m_Storage.save(DIAG_STORAGE_NAME,document) === true;
         }
         catch(diagError:Error) { return false; }
      }

      private function pollRelayStatus() : void
      {
         if(this.m_RelayPresent || this.m_RelayLoading) return;
         this.m_RelayRetryTicks++;
         if(this.m_RelayRetryTicks % 8 != 0) return;
         if(this.m_RelayAttempt >= RELAY_PATHS.length) this.m_RelayAttempt = 0;
         this.m_RelayLoading = true;
         this.m_RelayLoader = new URLLoader();
         this.m_RelayLoader.addEventListener(Event.COMPLETE,this.onRelayStatus);
         this.m_RelayLoader.addEventListener(IOErrorEvent.IO_ERROR,this.onRelayStatusError);
         this.m_RelayLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onRelayStatusError);
         try { this.m_RelayLoader.load(new URLRequest(RELAY_PATHS[this.m_RelayAttempt])); }
         catch(relayLoadError:Error)
         {
            this.m_RelayLoading = false;
            this.m_RelayAttempt++;
         }
      }

      private function onRelayStatus(param1:Event) : void
      {
         this.m_RelayLoading = false;
         var document:String = "";
         try { document = String(this.m_RelayLoader.data); } catch(dataError:Error) { document = ""; }
         var fields:Object = this.parseFlatJson(document);
         if(fields["relay"] == "1" && fields["build"] == RELAY_BUILD)
         {
            this.m_RelayPresent = true;
            return;
         }
         this.m_RelayAttempt++;
         if(this.m_RelayAttempt >= RELAY_PATHS.length) this.m_RelayAttempt = 0;
      }

      private function onRelayStatusError(param1:Event) : void
      {
         this.m_RelayLoading = false;
         this.m_RelayAttempt++;
         if(this.m_RelayAttempt >= RELAY_PATHS.length) this.m_RelayAttempt = 0;
      }

      private function writeStorageCommand(param1:String) : Boolean
      {
         if(!this.m_StorageReady || this.m_Storage == null) return false;
         try
         {
            var stamp:Number = new Date().getTime();
            if(stamp <= this.m_CommandStamp) stamp = this.m_CommandStamp + 1;
            this.m_CommandStamp = stamp;
            var document:String = "{\"stamp\":" + stamp + ",\"command\":\"" + this.sanitizeCommand(param1) + "\"}";
            return this.m_Storage.save(document) === true;
         }
         catch(storageError:Error) { return false; }
      }

      private function encodeCommand(param1:String) : String
      {
         var result:String = param1.split("%").join("%25");
         result = result.split("|").join("%7C");
         result = result.split("#").join("%23");
         return result;
      }

      private function sanitize(param1:String) : String
      {
         return this.sanitizeText(param1,32);
      }

      private function sanitizeCommand(param1:String) : String
      {
         return this.sanitizeText(param1,900);
      }

      private function sanitizeText(param1:String, param2:uint) : String
      {
         var result:String = "";
         var index:uint = 0;
         while(index < param1.length && index < param2)
         {
            var character:String = param1.charAt(index);
            if(character == "\"" || character == "\\" || character == "\n" || character == "\r") result += " ";
            else result += character;
            index++;
         }
         return result;
      }

      private function writeSharedCommand(param1:String) : Boolean
      {
         try
         {
            var store:SharedObject = SharedObject.getLocal("r76radio");
            if(store == null) return false;
            var sequence:Number = Number(store.data.seq);
            if(isNaN(sequence)) sequence = 0;
            sequence++;
            store.data.seq = sequence;
            store.data.stamp = sequence;
            store.data.command = this.sanitizeCommand(param1);
            try { store.flush(); } catch(flushError:Error) {}
            return true;
         }
         catch(sharedError:Error) { return false; }
      }

      private function relayState() : String
      {
         if(this.m_StorageReady) return "direct";
         if(this.m_RelayPresent) return "ok";
         try
         {
            var store:SharedObject = SharedObject.getLocal("r76radio");
            if(store != null && store.data.relayBuild == RELAY_BUILD)
            {
               if(store.data.relay != null) return "ok";
               if(store.data.relayPresent != null) return "loaded";
            }
         }
         catch(relayError:Error) {}
         return this.m_HudTools != null ? "hudtools" : "none";
      }

      private function refreshSpotifyState() : void
      {
         try { this.refreshSpotifyStateCore(); }
         catch(error:Error)
         {
            this.m_Diagnostic = "poll error " + error.errorID + " " + error.message;
            this.setLabelText(this.m_StateField,"ERROR");
            this.setLabelText(this.m_StatusField,"Status: " + this.m_Diagnostic);
         }
      }

      private function refreshSpotifyStateCore() : void
      {
         if(this.m_FileFields != null)
         {
            this.applyState(this.m_FileFields);
            this.m_FileLoadedAt++;
            if(this.m_FileLoading)
            {
               this.m_FileReloadTicks++;
               if(this.m_FileReloadTicks > 20) this.abortFileReload();
            }
            else if(this.m_FileLoadedAt > 6)
            {
               this.m_FileLoadedAt = 0;
               this.reloadFileState();
            }
            return;
         }
         if(this.m_FileLoading)
         {
            this.m_FileLoadTicks++;
            if(this.m_FileLoadTicks <= 20)
            {
               this.setLabelText(this.m_StatusField,"Status: loading state file");
               return;
            }
            this.abortFileLoad();
            this.m_FileAttempt++;
         }
         this.tryFileState();
      }

      private function reloadFileState() : void
      {
         if(this.m_FileGoodPath.length == 0 || this.m_FileLoading) return;
         this.m_FileLoading = true;
         this.m_FileReloadTicks = 0;
         this.m_FileReload = new URLLoader();
         this.m_FileReload.addEventListener(Event.COMPLETE,this.onFileReloadComplete);
         this.m_FileReload.addEventListener(IOErrorEvent.IO_ERROR,this.onFileReloadError);
         this.m_FileReload.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onFileReloadError);
         this.m_FileReload.load(new URLRequest(this.m_FileGoodPath));
      }

      private function onFileReloadComplete(param1:Event) : void
      {
         this.m_FileLoading = false;
         this.m_FileLoadedAt = 0;
         this.m_FileReloadTicks = 0;
         var document:String = "";
         try { document = String(this.m_FileReload.data); } catch(dataError:Error) { document = ""; }
         var fields:Object = this.parseFlatJson(document);
         if(fields["state"] != null)
         {
            this.m_FileReloadErrors = 0;
            this.m_FileFields = fields;
            this.applyState(fields);
            this.maybeLoadArtwork(fields);
            this.retryArtworkIfMissing();
            return;
         }
         this.m_FileReloadErrors++;
         this.handleReloadFailure();
      }

      private function onFileReloadError(param1:Event) : void
      {
         this.m_FileLoading = false;
         this.m_FileLoadedAt = 0;
         this.m_FileReloadTicks = 0;
         this.m_FileReloadErrors++;
         this.handleReloadFailure();
      }

      private function handleReloadFailure() : void
      {
         if(this.m_FileReloadErrors >= 3) this.setLabelText(this.m_StatusField,"Status: state stale");
         if(this.m_FileReloadErrors >= 12)
         {
            this.m_FileFields = null;
            this.m_FileLoadedAt = 0;
            this.m_FileReloadErrors = 0;
         }
      }

      private function abortFileReload() : void
      {
         if(this.m_FileReload != null)
         {
            this.m_FileReload.removeEventListener(Event.COMPLETE,this.onFileReloadComplete);
            this.m_FileReload.removeEventListener(IOErrorEvent.IO_ERROR,this.onFileReloadError);
            this.m_FileReload.removeEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onFileReloadError);
            try { this.m_FileReload.close(); } catch(closeError:Error) {}
         }
         this.m_FileReload = null;
         this.m_FileLoading = false;
         this.m_FileReloadTicks = 0;
         this.m_FileLoadedAt = 0;
      }

      private function artworkPaths() : Array
      {
         if(this.m_FileGoodPath.length == 0)
         {
            var fallback:Array = [];
            var index:uint = 0;
            while(index < DEFAULT_ART_BASES.length)
            {
               var inner:uint = 0;
               while(inner < ART_NAMES.length)
               {
                  fallback.push(DEFAULT_ART_BASES[index] + ART_NAMES[inner]);
                  inner++;
               }
               index++;
            }
            return fallback;
         }
         var slash:int = this.m_FileGoodPath.lastIndexOf("/");
         var base:String = slash >= 0 ? this.m_FileGoodPath.substring(0,slash + 1) : "";
         var paths:Array = [];
         var i:uint = 0;
         while(i < ART_NAMES.length)
         {
            paths.push(base + ART_NAMES[i]);
            i++;
         }
         var index:uint = 0;
         while(index < DEFAULT_ART_BASES.length)
         {
            var inner:uint = 0;
            while(inner < ART_NAMES.length)
            {
               var candidate:String = DEFAULT_ART_BASES[index] + ART_NAMES[inner];
               if(paths.indexOf(candidate) < 0) paths.push(candidate);
               inner++;
            }
            index++;
         }
         return paths;
      }

      private function maybeLoadArtwork(param1:Object) : void
      {

         if(this.m_StreamActive)
         {
            this.m_ArtSignature = "";
            this.abortArtworkLoad();
            this.hideArtwork();
            return;
         }
         var signature:String = String(param1["artist"]) + "|" + String(param1["title"]);
         if(signature == this.m_ArtSignature) return;
         this.m_ArtSignature = signature;
         this.m_ArtAttempt = 0;
         this.abortArtworkLoad();
         if(!this.m_ShowArtwork) return;
         this.loadArtwork();
      }

      private function retryArtworkIfMissing() : void
      {
         if(!this.m_ShowArtwork || this.m_StreamActive || !this.m_PanelVisible || this.m_ArtImage != null || this.m_ArtLoading || this.m_ArtSignature.length == 0) return;
         this.m_ArtAttempt = 0;
         this.loadArtwork();
      }

      private function loadArtwork() : void
      {
         if(!this.m_ShowArtwork) return;
         var paths:Array = this.artworkPaths();
         if(this.m_ArtLoading || this.m_ArtAttempt >= paths.length) return;
         this.m_ArtLoading = true;
         this.m_ArtLoadTicks = 0;
         this.m_ArtLoadingSignature = this.m_ArtSignature;
         this.setLabelText(this.m_ArtStatusField,"Art: load " + this.m_ArtAttempt);
         this.m_ArtLoader = new URLLoader();
         this.m_ArtLoader.addEventListener(Event.COMPLETE,this.onArtworkDocument);
         this.m_ArtLoader.addEventListener(IOErrorEvent.IO_ERROR,this.onArtworkError);
         this.m_ArtLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onArtworkError);
         try { this.m_ArtLoader.load(new URLRequest(paths[this.m_ArtAttempt])); }
         catch(loadError:Error)
         {
            this.m_ArtLoading = false;
            this.m_ArtAttempt++;
            this.loadArtwork();
         }
      }

      private function onArtworkDocument(param1:Event) : void
      {
         if(!this.m_ArtLoading) return;
         var document:String = "";
         try { document = String(this.m_ArtLoader.data); } catch(dataError:Error) { document = ""; }
         this.releaseArtworkLoader();
         this.setLabelText(this.m_ArtStatusField,"Art: doc " + document.length);
         if(document.length == 0)
         {
            this.onArtworkError(param1);
            return;
         }
         var newline:int = document.indexOf("\n");
         if(newline <= 0)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: bad header");
            this.onArtworkError(param1);
            return;
         }
         var header:String = document.substring(0,newline);
         var space:int = header.indexOf(" ");
         if(space <= 0)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: bad header");
            this.onArtworkError(param1);
            return;
         }
         var width:int = int(header.substring(0,space));
         var height:int = int(header.substring(space + 1));
         if(width <= 0 || height <= 0 || width > 512 || height > 512)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: bad size " + width + "x" + height);
            this.onArtworkError(param1);
            return;
         }
         var decoded:ByteArray = this.decodeBase64(document.substring(newline + 1));
         if(decoded.length != width * height * 4)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: px " + decoded.length + "/" + (width * height * 4));
            this.onArtworkError(param1);
            return;
         }
         this.m_ArtPixels = decoded;
         this.m_ArtPixelWidth = width;
         this.m_ArtPixelHeight = height;
         var pixels:BitmapData = null;
         try
         {
            pixels = new BitmapData(width,height);
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
                  pixels.setPixel(column,row,(red << 16) | (green << 8) | blue);
                  column++;
               }
               row++;
            }
         }
         catch(pixelError:Error)
         {
            this.loadArtworkBmp(decoded,width,height);
            return;
         }
         if(pixels == null)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: decode failed");
            this.onArtworkError(param1);
            return;
         }
         var bitmap:Bitmap = new Bitmap(pixels);
         bitmap.smoothing = true;
         this.m_ArtImage = bitmap;
         this.m_ArtLoading = false;
         this.m_ArtLoadTicks = 0;
         this.setLabelText(this.m_ArtStatusField,"Art: decoded " + width + "x" + height);
         this.finishArtwork(width,height);
      }

      private function loadArtworkBmp(param1:ByteArray, param2:int, param3:int) : void
      {
         if(!this.m_ArtLoading) return;
         this.m_ArtPixelWidth = param2;
         this.m_ArtPixelHeight = param3;
         this.setLabelText(this.m_ArtStatusField,"Art: bmp " + param2 + "x" + param3);
         var bytes:ByteArray = this.buildBitmapBytes(param1,param2,param3);
         var loader:Loader = new Loader();
         this.m_ArtImage = loader;
         loader.contentLoaderInfo.addEventListener(Event.COMPLETE,this.onArtworkBmpComplete);
         loader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,this.onArtworkBmpError);
         loader.contentLoaderInfo.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onArtworkBmpError);
         try { loader.loadBytes(bytes); }
         catch(bmpError:Error)
         {
            this.onArtworkError(new Event(Event.COMPLETE));
         }
      }

      private function buildBitmapBytes(param1:ByteArray, param2:int, param3:int) : ByteArray
      {
         var output:ByteArray = new ByteArray();
         output.endian = Endian.LITTLE_ENDIAN;
         var rowBytes:int = (param2 * 3 + 3) & ~3;
         var imageBytes:int = rowBytes * param3;
         output.writeByte(0x42);
         output.writeByte(0x4D);
         output.writeInt(54 + imageBytes);
         output.writeShort(0);
         output.writeShort(0);
         output.writeInt(54);
         output.writeInt(40);
         output.writeInt(param2);
         output.writeInt(param3);
         output.writeShort(1);
         output.writeShort(24);
         output.writeInt(0);
         output.writeInt(imageBytes);
         output.writeInt(2835);
         output.writeInt(2835);
         output.writeInt(0);
         output.writeInt(0);
         param1.position = 0;
         var row:int = param3 - 1;
         while(row >= 0)
         {
            var column:int = 0;
            while(column < param2)
            {
               param1.readUnsignedByte();
               var red:uint = param1.readUnsignedByte();
               var green:uint = param1.readUnsignedByte();
               var blue:uint = param1.readUnsignedByte();
               output.writeByte(blue);
               output.writeByte(green);
               output.writeByte(red);
               column++;
            }
            var padding:int = rowBytes - param2 * 3;
            while(padding > 0)
            {
               output.writeByte(0);
               padding--;
            }
            row--;
         }
         output.position = 0;
         return output;
      }

      private function onArtworkBmpComplete(param1:Event) : void
      {
         if(!this.m_ArtLoading) return;
         this.m_ArtLoading = false;
         this.m_ArtLoadTicks = 0;
         this.setLabelText(this.m_ArtStatusField,"Art: bmp ok " + this.m_ArtPixelWidth + "x" + this.m_ArtPixelHeight);
         this.finishArtwork(this.m_ArtPixelWidth,this.m_ArtPixelHeight);
      }

      private function onArtworkBmpError(param1:Event) : void
      {
         this.m_ArtImage = null;
         if(this.m_ArtPixels != null)
         {
            this.loadArtworkMosaic(this.m_ArtPixels,this.m_ArtPixelWidth,this.m_ArtPixelHeight);
            return;
         }
         this.onArtworkError(param1);
      }

      private function loadArtworkMosaic(param1:ByteArray, param2:int, param3:int) : void
      {
         if(!this.m_ArtLoading) return;
         var cells:int = 96;
         var columns:int = Math.min(cells,param2);
         var rows:int = Math.min(cells,param3);
         var art:MovieClip = new MovieClip();
         var drawing:Graphics = art.graphics;
         var row:int = 0;
         try
         {
            while(row < rows)
            {
               var sourceRow:int = int(row * param3 / rows);
               var column:int = 0;
               while(column < columns)
               {
                  var sourceColumn:int = int(column * param2 / columns);
                  param1.position = (sourceRow * param2 + sourceColumn) * 4;
                  param1.readUnsignedByte();
                  var red:int = param1.readUnsignedByte();
                  var green:int = param1.readUnsignedByte();
                  var blue:int = param1.readUnsignedByte();
                  drawing.beginFill((red << 16) | (green << 8) | blue,1);
                  drawing.drawRect(column,row,1,1);
                  drawing.endFill();
                  column++;
               }
               row++;
            }
         }
         catch(mosaicError:Error)
         {
            this.onArtworkError(new Event(Event.COMPLETE));
            return;
         }
         this.m_ArtImage = art;
         this.m_ArtLoading = false;
         this.m_ArtLoadTicks = 0;
         this.setLabelText(this.m_ArtStatusField,"Art: mosaic " + columns + "x" + rows);
         this.finishArtwork(columns,rows);
      }

      private function finishArtwork(param1:int, param2:int) : void
      {
         if(!this.m_PanelVisible)
         {
            this.hideArtwork();
            return;
         }
         try { this.placeArtwork(param1,param2); }
         catch(placeError:Error)
         {
            this.hideArtwork();
         }
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

      private function releaseArtworkLoader() : void
      {
         if(this.m_ArtLoader == null) return;
         this.m_ArtLoader.removeEventListener(Event.COMPLETE,this.onArtworkDocument);
         this.m_ArtLoader.removeEventListener(IOErrorEvent.IO_ERROR,this.onArtworkError);
         this.m_ArtLoader.removeEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onArtworkError);
         try { this.m_ArtLoader.close(); } catch(loaderCloseError:Error) {}
         this.m_ArtLoader = null;
      }

      private function abortArtworkLoad() : void
      {
         this.releaseArtworkLoader();
         if(this.m_ArtImage != null)
         {
            if(this.m_ArtImage is Loader)
            {
               var imageLoader:Loader = this.m_ArtImage as Loader;
               try
               {
                  imageLoader.contentLoaderInfo.removeEventListener(Event.COMPLETE,this.onArtworkBmpComplete);
                  imageLoader.contentLoaderInfo.removeEventListener(IOErrorEvent.IO_ERROR,this.onArtworkBmpError);
                  imageLoader.contentLoaderInfo.removeEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onArtworkBmpError);
               }
               catch(imageListenerError:Error) {}
               try { imageLoader.close(); } catch(imageCloseError:Error) {}
            }
            if(this.m_ArtImage.parent != null) this.m_ArtImage.parent.removeChild(this.m_ArtImage);
            this.m_ArtImage = null;
            if(this.m_PanelVisible && this.RadioWave_mc != null) this.RadioWave_mc.visible = true;
         }
         this.m_ArtLoading = false;
      }

      private function placeArtwork(param1:Number, param2:Number) : void
      {
         if(this.m_ArtImage == null) return;
         if(!this.m_PanelVisible)
         {
            this.hideArtwork();
            return;
         }
         var wave:DisplayObject = this.RadioWave_mc as DisplayObject;
         if(wave == null || wave.parent == null)
         {
            this.hideArtwork();
            return;
         }
         var host:DisplayObjectContainer = wave.parent;
         var waveRect:Rectangle = wave.getRect(host);
         if(waveRect.width <= 0 || waveRect.height <= 0)
         {
            var waveScaleX:Number = wave.scaleX;
            var waveScaleY:Number = wave.scaleY;
            if(waveScaleX == 0) waveScaleX = 1;
            if(waveScaleY == 0) waveScaleY = 1;
            waveRect = new Rectangle(
               wave.x,
               wave.y,
               Math.abs(RadioWave.WAVE_WIDTH * waveScaleX),
               Math.abs(RadioWave.MAX_AMP * waveScaleY));
         }
         var waveIndex:int = host.getChildIndex(wave);
         if(this.m_ArtImage.parent != null) this.m_ArtImage.parent.removeChild(this.m_ArtImage);
         if(waveIndex >= 0) host.addChildAt(this.m_ArtImage,waveIndex + 1);
         else host.addChild(this.m_ArtImage);

         var targetWidth:Number = waveRect.width - 16;
         var targetHeight:Number = targetWidth;
         if(targetWidth <= 0 || targetHeight <= 0 || param1 <= 0 || param2 <= 0)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: size " + targetWidth + "x" + targetHeight);
            this.hideArtwork();
            return;
         }
         var scale:Number = Math.min(targetWidth / param1,targetHeight / param2);
         var scaledWidth:Number = param1 * scale;
         var scaledHeight:Number = param2 * scale;
         this.m_ArtImage.scaleX = scale;
         this.m_ArtImage.scaleY = scale;
         this.m_ArtImage.x = waveRect.x + (targetWidth - scaledWidth) / 2;
         this.m_ArtImage.y = waveRect.y + (targetHeight - scaledHeight) / 2;
         this.setLabelText(this.m_ArtStatusField,"Art: ok " + param1 + "x" + param2);
         if(this.RadioWave_mc != null) this.RadioWave_mc.visible = false;
         if(this.DeadWave_mc != null) this.DeadWave_mc.visible = false;
      }

      private function onArtworkError(param1:Event) : void
      {
         this.releaseArtworkLoader();
         this.m_ArtLoading = false;
         this.m_ArtLoadTicks = 0;
         this.m_ArtAttempt++;
         if(this.m_ArtAttempt >= this.artworkPaths().length)
         {
            this.setLabelText(this.m_ArtStatusField,"Art: missing");
            this.hideArtwork();
            return;
         }
         this.setLabelText(this.m_ArtStatusField,"Art: retry " + this.m_ArtAttempt);
         this.loadArtwork();
      }

      private function hideArtwork() : void
      {
         this.abortArtworkLoad();
         var entry:Object = this.List_mc != null ? this.List_mc.selectedEntry : null;
         var hasSelection:Boolean = Boolean(entry);
         if(this.RadioWave_mc != null) this.RadioWave_mc.visible = hasSelection;
         if(this.DeadWave_mc != null) this.DeadWave_mc.visible = !hasSelection;
      }

      private function abortFileLoad() : void
      {
         if(this.m_FileLoader != null)
         {
            this.m_FileLoader.removeEventListener(Event.COMPLETE,this.onFileStateComplete);
            this.m_FileLoader.removeEventListener(IOErrorEvent.IO_ERROR,this.onFileStateError);
            this.m_FileLoader.removeEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onFileStateError);
            try { this.m_FileLoader.close(); } catch(closeError:Error) {}
         }
         this.m_FileLoader = null;
         this.m_FileLoading = false;
         this.m_FileLoadTicks = 0;
      }

      private function tryFileState() : void
      {
         if(this.m_FileLoading) return;
         if(this.m_FileAttempt >= FILE_STATE_PATHS.length)
         {
            this.m_FileRetryTicks++;
            if(this.m_FileRetryTicks < 6) return;
            this.m_FileRetryTicks = 0;
            this.m_FileAttempt = 0;
         }
         this.m_FileLoading = true;
         this.m_FileLoadTicks = 0;
         var request:URLRequest = new URLRequest(FILE_STATE_PATHS[this.m_FileAttempt]);
         this.m_FileLoader = new URLLoader();
         this.m_FileLoader.addEventListener(Event.COMPLETE,this.onFileStateComplete);
         this.m_FileLoader.addEventListener(IOErrorEvent.IO_ERROR,this.onFileStateError);
         this.m_FileLoader.addEventListener(SecurityErrorEvent.SECURITY_ERROR,this.onFileStateError);
         this.m_FileLoader.load(request);
      }

      private function onFileStateComplete(param1:Event) : void
      {
         this.m_FileLoading = false;
         this.m_FileLoadTicks = 0;
         var document:String = "";
         try { document = String(this.m_FileLoader.data); } catch(dataError:Error) { document = ""; }
         var fields:Object = this.parseFlatJson(document);
         if(fields["state"] != null)
         {
            this.m_FileGoodPath = FILE_STATE_PATHS[this.m_FileAttempt];
            this.m_FileLoadedAt = 0;
            this.m_FileReloadErrors = 0;
            this.m_FileFields = fields;
            this.applyState(fields);
            this.maybeLoadArtwork(fields);
            return;
         }
         this.m_FileAttempt++;
         this.tryFileState();
      }

      private function onFileStateError(param1:Event) : void
      {
         this.m_FileLoading = false;
         this.m_FileLoadTicks = 0;
         this.m_FileAttempt++;
         this.tryFileState();
         if(this.m_FileAttempt >= FILE_STATE_PATHS.length && !this.m_FileLoading)
         {
            this.m_Diagnostic = "state file unreadable";
            this.applyUnavailableState();
         }
      }

      private function applyUnavailableState() : void
      {
         var reason:String = this.m_Diagnostic != null ? this.m_Diagnostic : "Bridge offline";
         var fields:Object = {"state":"unavailable","status":"76Radio offline"};
         this.applyState(fields);
         this.setLabelText(this.m_StateField,"UNAVAILABLE");
         this.setLabelText(this.m_StatusField,"Status: " + reason);
         if(reason == "state file unreadable" && this.m_StatusField != null)
         {
            this.m_StatusField.visible = this.m_PanelVisible;
         }
         this.m_SpotifyPlaying = false;
         this.m_StreamActive = false;
         this.m_StreamStation = "";
         this.m_AudioPlaying = false;
         this.syncEntryState();
         if(this.List_mc != null) this.List_mc.InvalidateData();
      }

      private function applyState(param1:Object) : void
      {
         if(param1["state"] == null)
         {
            this.applyUnavailableState();
            return;
         }
         this.m_Diagnostic = null;
         var ready:Boolean = param1["ready"] != "0";
         var state:String = "unavailable";
         var title:String = "";
         var artist:String = "";
         var album:String = "";
         var session:Boolean = false;
         var spotify:Boolean = false;
         var durationMs:Number = 0;
         var positionMs:Number = 0;
         var capturedMs:Number = 0;
         var canNext:Boolean = false;
         var canPrev:Boolean = false;
         var canToggle:Boolean = false;
         var canSeek:Boolean = false;
         var stream:Boolean = false;
         if(ready)
         {
            state = String(param1["state"]);
            title = param1["title"] != null ? String(param1["title"]) : "";
            artist = param1["artist"] != null ? String(param1["artist"]) : "";
            album = param1["album"] != null ? String(param1["album"]) : "";
            session = param1["session"] == "1";
            spotify = param1["spotify"] == "1";
            durationMs = this.readNumber(param1,"durationMs");
            positionMs = this.readNumber(param1,"positionMs");
            capturedMs = this.readNumber(param1,"capturedMs");
            canNext = param1["canNext"] == "1";
            canPrev = param1["canPrevious"] == "1";
            canToggle = param1["canToggle"] == "1";
            canSeek = param1["canSeek"] == "1";
            stream = param1["stream"] == "1";
         }
         this.m_PlaybackState = ready ? state : "unavailable";
         this.m_DurationMs = durationMs;
         this.m_PositionMs = positionMs;
         this.m_CapturedMs = capturedMs;
         this.m_CanNext = canNext;
         this.m_CanPrev = canPrev;
         this.m_CanToggle = canToggle;
         this.m_CanSeek = canSeek;
         this.m_StreamActive = ready && stream;
         this.m_AudioPlaying = ready && state == "playing";

         this.m_SpotifyPlaying = this.m_AudioPlaying && !this.m_StreamActive;

         var advertisedStation:String = param1["station"] != null ? String(param1["station"]) : "";
         this.m_StreamStation = this.m_StreamActive ? (advertisedStation.length > 0 ? advertisedStation : title) : "";
         if(this.m_StreamActive || this.m_DurationMs <= 0) this.m_SeekPendingMs = -1;
         var seekable:Boolean = this.m_CanSeek && !this.m_StreamActive && this.m_DurationMs > 0;
         if(this.m_ProgressHit != null) this.m_ProgressHit.mouseEnabled = seekable;
         if(!seekable) this.setProgressHover(false);
         this.applyUiFlags(param1);
         this.setTitleText(title.length > 0 ? title : "No track");
         this.setLabelText(this.m_ArtistField,artist.length > 0 ? artist : "No artist");
         this.setLabelText(this.m_AlbumField,album.length > 0 ? album : "No album");
         this.updateTimeField();
         if(ready)
         {
            this.setLabelText(this.m_StateField,state == "playing" ? "PLAYING" : (state == "paused" ? "PAUSED" : "STOPPED"));
            if(this.m_StreamActive) this.setLabelText(this.m_StatusField,"Status: Internet stream | relay " + this.relayState());
            else if(session) this.setLabelText(this.m_StatusField,"Status: Connected | relay " + this.relayState());
            else if(spotify) this.setLabelText(this.m_StatusField,"Status: Spotify idle | relay " + this.relayState());
            else this.setLabelText(this.m_StatusField,"Status: Spotify not running | relay " + this.relayState());
         }
         else
         {
            this.setLabelText(this.m_StateField,"STARTING");
            this.setLabelText(this.m_StatusField,"Status: 76Radio starting | relay " + this.relayState());
         }

         if(this.m_StreamActive) this.setLabelText(this.m_PlayButton,"STOP");
         else this.setLabelText(this.m_PlayButton,this.m_SpotifyPlaying ? "PAUSE" : "PLAY");
         this.applyButtonState();
         var entryStateChanged:Boolean = this.syncEntryState();
         this.setPanelVisible(this.m_SpotifySelected || this.m_StreamActive);
         if(entryStateChanged || this.m_LastActiveState != this.m_SpotifyPlaying)
         {
            this.m_LastActiveState = this.m_SpotifyPlaying;
            if(this.List_mc != null && this.List_mc.entryList != null) this.List_mc.InvalidateData();
            if(this.List_mc != null) this.updateRadioFrequency();
         }
      }

      private function createSpotifyPanel() : void
      {
         this.m_NowPlayingField = this.createLabel("NOW PLAYING",11);
         this.m_NowPlayingField.x = 566; this.m_NowPlayingField.y = 404;

         this.m_TitleClip = new Sprite();
         this.m_TitleClip.x = 566; this.m_TitleClip.y = 420;
         this.m_TitleClip.mouseEnabled = false;
         addChild(this.m_TitleClip);

         this.m_TitleMask = new Sprite();
         this.m_TitleMask.graphics.beginFill(0x000000,1);
         this.m_TitleMask.graphics.drawRect(566,420,250,24);
         this.m_TitleMask.graphics.endFill();
         this.m_TitleMask.mouseEnabled = false;
         addChild(this.m_TitleMask);
         this.m_TitleClip.mask = this.m_TitleMask;

         this.m_TitleField = this.createLabel("No track",18);
         this.m_TitleField.x = 0; this.m_TitleField.y = 0;
         this.m_TitleField.width = 250;
         this.m_TitleField.mouseEnabled = false;
         removeChild(this.m_TitleField);
         this.m_TitleClip.addChild(this.m_TitleField);

         this.m_ArtistField = this.createLabel("No artist",14);
         this.m_ArtistField.x = 566; this.m_ArtistField.y = 448;
         this.m_ArtistField.width = 300;

         this.m_AlbumField = this.createLabel("No album",12);
         this.m_AlbumField.x = 566; this.m_AlbumField.y = 466;
         this.m_AlbumField.width = 300;
         this.m_AlbumField.mouseEnabled = false;
         this.m_AlbumField.wordWrap = false; this.m_AlbumField.multiline = false;

         this.m_TimeField = this.createLabel("--:-- / --:--",12);
         this.m_TimeField.x = 566; this.m_TimeField.y = 486;
         this.m_TimeField.width = 300;
         this.m_TimeField.mouseEnabled = false;
         this.m_TimeField.wordWrap = false; this.m_TimeField.multiline = false;

         this.m_ProgressBg = new Sprite();
         this.m_ProgressBg.x = 566; this.m_ProgressBg.y = 504;
         this.m_ProgressBg.mouseEnabled = false;
         this.m_ProgressBg.graphics.beginFill(0x333333,1);
         this.m_ProgressBg.graphics.drawRect(0,0,PANEL_PROGRESS_WIDTH,2);
         this.m_ProgressBg.graphics.endFill();
         addChild(this.m_ProgressBg);

         this.m_ProgressFill = new Sprite();
         this.m_ProgressFill.x = 566; this.m_ProgressFill.y = 504;
         this.m_ProgressFill.mouseEnabled = false;
         this.m_ProgressFill.graphics.beginFill(PANEL_COLOR,1);
         this.m_ProgressFill.graphics.drawRect(0,0,PANEL_PROGRESS_WIDTH,2);
         this.m_ProgressFill.graphics.endFill();
         this.m_ProgressFill.scaleX = 0;
         addChild(this.m_ProgressFill);

         this.m_ProgressKnob = new Sprite();
         this.m_ProgressKnob.x = 566; this.m_ProgressKnob.y = 505;
         this.m_ProgressKnob.mouseEnabled = false;
         this.m_ProgressKnob.graphics.beginFill(PANEL_COLOR,1);
         this.m_ProgressKnob.graphics.drawCircle(0, 0, 4);
         this.m_ProgressKnob.graphics.endFill();
         addChild(this.m_ProgressKnob);

         this.m_ProgressHit = new Sprite();
         this.m_ProgressHit.x = 566; this.m_ProgressHit.y = 496;
         this.m_ProgressHit.mouseEnabled = false;
         this.m_ProgressHit.buttonMode = true;
         this.m_ProgressHit.useHandCursor = true;
         this.m_ProgressHit.graphics.beginFill(0x000000,0.01);
         this.m_ProgressHit.graphics.drawRect(0,0,PANEL_PROGRESS_WIDTH,14);
         this.m_ProgressHit.graphics.endFill();
         this.m_ProgressHit.addEventListener(MouseEvent.CLICK,this.onProgressClick);
         this.m_ProgressHit.addEventListener(MouseEvent.MOUSE_OVER,this.onProgressOver);
         this.m_ProgressHit.addEventListener(MouseEvent.MOUSE_OUT,this.onProgressOut);
         addChild(this.m_ProgressHit);

         this.m_StateField = this.createLabel("STOPPED",13);
         this.m_StateField.x = 566; this.m_StateField.y = 516;

         this.m_StatusField = this.createLabel("Status: waiting",13);
         this.m_StatusField.x = 566; this.m_StatusField.y = 564;
         this.m_StatusField.width = 300;

         this.m_ArtStatusField = this.createLabel("Art: wait",11);
         this.m_ArtStatusField.x = 566; this.m_ArtStatusField.y = 592;
         this.m_StatusField.multiline = true; this.m_StatusField.wordWrap = true; this.m_StatusField.height = 26;
         this.m_StatusField.visible = false;
         this.m_ArtStatusField.visible = false;
         this.m_TitleField.wordWrap = false; this.m_TitleField.multiline = false;

         this.m_PreviousButton = this.createLabel("< PREV",15,null,70);
         this.m_PreviousButton.x = 588; this.m_PreviousButton.y = 542;
         this.m_PreviousButton.addEventListener(MouseEvent.CLICK,this.onPreviousClick);

         this.m_PlayButton = this.createLabel("PLAY",15,null,70);
         this.m_PlayButton.x = 681; this.m_PlayButton.y = 542;
         this.m_PlayButton.addEventListener(MouseEvent.CLICK,this.onPlayClick);

         this.m_NextButton = this.createLabel("NEXT >",15,null,70);
         this.m_NextButton.x = 774; this.m_NextButton.y = 542;
         this.m_NextButton.addEventListener(MouseEvent.CLICK,this.onNextClick);

         this.setPanelVisible(false);
         this.applyButtonState();
         this.refreshSpotifyState();
      }

      private function createLabel(param1:String, param2:uint, param3:String = null, param4:Number = 280) : TextField
      {
         var face:String = param3 != null ? param3 : PANEL_FONT_BOLD;
         var field:TextField = new TextField();
         var format:TextFormat = new TextFormat(face,param2,PANEL_COLOR);
         field.defaultTextFormat = format;
         field.embedFonts = this.m_LabelEmbed;
         field.selectable = false; field.multiline = false; field.mouseEnabled = true;
         field.text = param1; field.textColor = PANEL_COLOR;
         field.width = param4; field.height = param2 + 10;
         field.visible = this.m_PanelVisible;
         this.m_Labels.push({"field":field,"size":param2});
         addChild(field);
         return field;
      }

      private function setLabelText(param1:TextField, param2:String) : void
      {
         if(param1 == null) return;
         if(param1.text != param2)
         {
            param1.text = param2;
            var size:uint = param1.defaultTextFormat != null && param1.defaultTextFormat.size > 0 ? uint(param1.defaultTextFormat.size) : 14;
            this.applyLabelStyleTo(param1,size);
         }
         param1.textColor = PANEL_COLOR;
      }

      private function applyButtonState() : void
      {
         if(this.m_PreviousButton != null) this.m_PreviousButton.textColor = this.m_CanPrev ? PANEL_COLOR : PANEL_DISABLED_COLOR;
         if(this.m_PlayButton != null) this.m_PlayButton.textColor = this.m_CanToggle ? PANEL_COLOR : PANEL_DISABLED_COLOR;
         if(this.m_NextButton != null) this.m_NextButton.textColor = this.m_CanNext ? PANEL_COLOR : PANEL_DISABLED_COLOR;
      }

      private function readNumber(param1:Object, param2:String) : Number
      {
         if(param1 == null || param1[param2] == null) return 0;
         var value:Number = Number(param1[param2]);
         if(isNaN(value) || value < 0) return 0;
         return value;
      }

      private function onProgressClick(param1:MouseEvent) : void
      {
         if(this.m_StreamActive || this.m_DurationMs <= 0 || !this.m_CanSeek) return;
         var percent:Number = param1.localX / PANEL_PROGRESS_WIDTH;
         if(isNaN(percent) || percent < 0) percent = 0;
         if(percent > 1) percent = 1;
         var target:Number = Math.round(percent * this.m_DurationMs);
         if(target < 0) target = 0;
         if(target > this.m_DurationMs) target = this.m_DurationMs;
         var now:Number = new Date().getTime();
         this.m_SeekPendingMs = target;
         this.m_SeekRequestedAt = now;
         this.m_SeekPendingUntil = now + 2500;
         this.m_PositionMs = target;
         this.m_CapturedMs = now;
         this.updateTimeField();
         this.sendCommand("seek:" + Math.round(target));
      }

      private function onProgressOver(param1:MouseEvent) : void
      {
         this.setProgressHover(true);
      }

      private function onProgressOut(param1:MouseEvent) : void
      {
         this.setProgressHover(false);
      }

      private function setProgressHover(param1:Boolean) : void
      {
         if(this.m_ProgressHover == param1) return;
         this.m_ProgressHover = param1;
         if(this.m_ProgressBg == null) return;
         var drawing:Graphics = this.m_ProgressBg.graphics;
         drawing.clear();
         drawing.beginFill(param1 ? 0x666666 : 0x333333,1);
         drawing.drawRect(0,0,PANEL_PROGRESS_WIDTH,2);
         drawing.endFill();
      }

      private function updateTimeField() : void
      {
         if(this.m_TimeField == null) return;
         if(this.m_DurationMs <= 0)
         {
            this.setLabelText(this.m_TimeField,this.m_StreamActive ? "LIVE" : "--:-- / --:--");
            if(this.m_ProgressFill != null) this.m_ProgressFill.scaleX = 0;
            if(this.m_ProgressKnob != null) this.m_ProgressKnob.x = 566;
            return;
         }
         var now:Number = new Date().getTime();
         if(this.m_SeekPendingMs >= 0 && now >= this.m_SeekPendingUntil) this.m_SeekPendingMs = -1;
         var position:Number = this.m_SeekPendingMs >= 0 ? this.m_SeekPendingMs : this.m_PositionMs;
         var capturedMs:Number = this.m_SeekPendingMs >= 0 ? this.m_SeekRequestedAt : this.m_CapturedMs;
         if(this.m_PlaybackState == "playing" && capturedMs > 0)
         {
            var elapsed:Number = now - capturedMs;
            if(elapsed > 0) position += elapsed;
         }
         if(position < 0) position = 0;
         if(position > this.m_DurationMs) position = this.m_DurationMs;
         this.setLabelText(this.m_TimeField,this.formatPlaybackTime(position) + " / " + this.formatPlaybackTime(this.m_DurationMs));

         if(this.m_ProgressFill != null && this.m_ProgressKnob != null)
         {
            var progress:Number = position / this.m_DurationMs;
            if(isNaN(progress) || progress < 0) progress = 0;
            if(progress > 1) progress = 1;
            this.m_ProgressFill.scaleX = progress;
            this.m_ProgressKnob.x = 566 + (PANEL_PROGRESS_WIDTH * progress);
         }
      }

      private function formatPlaybackTime(param1:Number) : String
      {
         if(isNaN(param1) || param1 < 0) param1 = 0;
         var totalSeconds:Number = Math.floor(param1 / 1000);
         var hours:Number = Math.floor(totalSeconds / 3600);
         var minutes:Number = Math.floor((totalSeconds - hours * 3600) / 60);
         var seconds:Number = totalSeconds - hours * 3600 - minutes * 60;
         var secondsText:String = seconds < 10 ? "0" + seconds : String(seconds);
         if(hours > 0)
         {
            var minutesText:String = minutes < 10 ? "0" + minutes : String(minutes);
            return hours + ":" + minutesText + ":" + secondsText;
         }
         return minutes + ":" + secondsText;
      }

      private function onPreviousClick(param1:MouseEvent) : void
      {
         if(!this.m_CanPrev)
         {
            GlobalFunc.PlayMenuSound("UIMenuCancel");
            return;
         }
         GlobalFunc.PlayMenuSound("UIMenuFocus");
         this.sendCommand("previous");
      }

      private function onPlayClick(param1:MouseEvent) : void
      {
         if(!this.m_CanToggle)
         {
            GlobalFunc.PlayMenuSound("UIMenuCancel");
            return;
         }
         GlobalFunc.PlayMenuSound("UIMenuFocus");

         if(this.m_StreamActive) this.sendCommand("pause");
         else this.sendCommand("toggle");
      }

      private function onNextClick(param1:MouseEvent) : void
      {
         if(!this.m_CanNext)
         {
            GlobalFunc.PlayMenuSound("UIMenuCancel");
            return;
         }
         GlobalFunc.PlayMenuSound("UIMenuFocus");
         this.sendCommand("next");
      }
   }
}
