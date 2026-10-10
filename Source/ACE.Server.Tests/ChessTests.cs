using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using ACE.Entity;
using ACE.Entity.Enum;
using ACE.Entity.Enum.Properties;
using ACE.Entity.Models;
using ACE.Server.Entity;
using ACE.Server.Entity.Chess;
using ACE.Server.Managers;
using ACE.Server.WorldObjects;
using System.Linq;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ACE.Server.Tests
{
    [TestClass]
    public class ChessTests
    {
        private static ChessPieceCoord C(int x, int y) => new ChessPieceCoord(x, y);
        private static ChessMoveResult Move(ChessLogic board, int x, int y, int tx, int ty)
        {
            var result = board.DoMove(board.Turn, C(x, y), C(tx, ty));
            Assert.IsGreaterThan(0, (int)result);
            return result;
        }

        [TestMethod]
        public void CompleteGameAndRejectedMoves()
        {
            var b = new ChessLogic();
            Assert.AreEqual(ChessMoveResult.BadMoveNotYourTurn, b.DoMove(ChessColor.Black, C(4,6), C(4,4)));
            Assert.AreEqual(ChessMoveResult.BadMoveDestination, b.DoMove(ChessColor.White, C(4,1), C(8,3)));
            Assert.IsLessThan(0, (int)b.DoMove(ChessColor.White, C(0,0), C(0,3)));
            Assert.AreEqual(ChessColor.White, b.Turn);
            Move(b,5,1,5,2); Move(b,4,6,4,4); Move(b,6,1,6,3);
            var mate = Move(b,3,7,7,3);
            Assert.IsTrue(mate.HasFlag(ChessMoveResult.OKMoveCheckmate));
            Assert.IsTrue(b.InCheckmate(ChessColor.White, true));
            Assert.AreEqual(4, b.History.Count);
        }

        [TestMethod]
        public void CastlingEnPassantAndPromotion()
        {
            var b = new ChessLogic();
            Move(b,4,1,4,3); Move(b,4,6,4,4);
            Move(b,6,0,5,2); Move(b,1,7,2,5);
            Move(b,5,0,2,3); Move(b,6,7,5,5);
            Move(b,4,0,6,0);
            Assert.AreEqual(ChessPieceType.King, b.GetPiece(C(6,0)).Type);
            Assert.AreEqual(ChessPieceType.Rook, b.GetPiece(C(5,0)).Type);
            Assert.IsNull(b.GetPiece(C(7,0)));
            b = new ChessLogic();
            Move(b,4,1,4,3); Move(b,0,6,0,5); Move(b,4,3,4,4); Move(b,3,6,3,4);
            Move(b,4,4,3,5);
            Assert.IsNull(b.GetPiece(C(3,4)));
            Assert.AreEqual(ChessColor.White, b.GetPiece(C(3,5)).Color);
            b = new ChessLogic(); Array.Clear(b.Board);
            b.AddPiece(ChessColor.White, ChessPieceType.King,4,0);
            b.AddPiece(ChessColor.Black, ChessPieceType.King,4,7);
            b.AddPiece(ChessColor.White, ChessPieceType.Pawn,0,6);
            Assert.IsTrue(Move(b,0,6,0,7).HasFlag(ChessMoveResult.OKMovePromotion));
            Assert.AreEqual(ChessPieceType.Queen,b.GetPiece(C(0,7)).Type);
        }

        [TestMethod]
        public void LegalSearchAndUndoPreserveEveryBoardField()
        {
            var b = new ChessLogic();
            // Exercise reversible search after captures, en passant, castling and promotion.
            foreach (var step in new[] { new[]{4,1,4,3},new[]{0,6,0,5},new[]{4,3,4,4},new[]{3,6,3,4} })
                Move(b,step[0],step[1],step[2],step[3]);
            CheckUndo(b,4,4,3,5);
            b = new ChessLogic();
            foreach (var step in new[] { new[]{4,1,4,3},new[]{4,6,4,4},new[]{6,0,5,2},new[]{1,7,2,5},new[]{5,0,2,3},new[]{6,7,5,5} })
                Move(b,step[0],step[1],step[2],step[3]);
            CheckUndo(b,4,0,6,0);
            CheckUndo(b,5,2,4,4);
            b = new ChessLogic(); Array.Clear(b.Board);
            b.AddPiece(ChessColor.White,ChessPieceType.King,4,0);
            b.AddPiece(ChessColor.Black,ChessPieceType.King,4,7);
            b.AddPiece(ChessColor.White,ChessPieceType.Pawn,0,6);
            b.AddPiece(ChessColor.Black,ChessPieceType.Rook,1,7);
            CheckUndo(b,0,6,1,7);
            b=new ChessLogic();Array.Clear(b.Board);
            b.AddPiece(ChessColor.White,ChessPieceType.King,4,0);
            b.AddPiece(ChessColor.Black,ChessPieceType.King,4,7);
            b.AddPiece(ChessColor.White,ChessPieceType.Bishop,1,6);
            b.AddPiece(ChessColor.Black,ChessPieceType.Rook,0,7);
            Move(b,1,6,0,7);
            Assert.IsFalse(b.Castling[1].HasFlag(ChessMoveFlag.QueenSideCastle));
            b.UndoMove(1);
            Assert.IsTrue(b.Castling[1].HasFlag(ChessMoveFlag.QueenSideCastle));
        }

        private static string Snapshot(ChessLogic b) =>
            $"{b.Turn}:{b.Move}:{b.HalfMove}:{b.History.Count}:{string.Join(',',b.Castling)}:{b.EnPassantCoord?.X}:{b.EnPassantCoord?.Y}:" +
            string.Join(';',b.Board.Select(p => p == null ? "-" : $"{p.Color},{p.Type},{p.Coord.X},{p.Coord.Y},{p.Guid.Full}"));

        private static void CheckUndo(ChessLogic b,int x,int y,int tx,int ty)
        {
            var before=Snapshot(b);
            Assert.IsTrue(b.HasLegalMove(b.Turn)); Assert.AreEqual(before,Snapshot(b));
            Move(b,x,y,tx,ty);b.UndoMove(1);Assert.AreEqual(before,Snapshot(b));
        }

        [TestMethod]
        public void SelfCheckStalemateAndCastlingThroughAttack()
        {
            var b=new ChessLogic();Array.Clear(b.Board);
            b.AddPiece(ChessColor.White,ChessPieceType.King,4,0);
            b.AddPiece(ChessColor.White,ChessPieceType.Rook,4,1);
            b.AddPiece(ChessColor.Black,ChessPieceType.King,0,7);
            b.AddPiece(ChessColor.Black,ChessPieceType.Rook,4,7);
            var before=Snapshot(b);
            Assert.AreEqual(ChessMoveResult.BadMoveSelfCheck,b.DoMove(ChessColor.White,C(4,1),C(5,1)));
            Assert.AreEqual(before,Snapshot(b));
            b=new ChessLogic();Array.Clear(b.Board);
            b.AddPiece(ChessColor.White,ChessPieceType.King,4,0);
            b.AddPiece(ChessColor.White,ChessPieceType.Rook,7,0);
            b.AddPiece(ChessColor.Black,ChessPieceType.King,0,7);
            b.AddPiece(ChessColor.Black,ChessPieceType.Rook,5,7);
            Assert.IsTrue(b.CanAttack(ChessColor.Black,C(5,0)));
            Assert.IsLessThan(0,(int)b.DoMove(ChessColor.White,C(4,0),C(6,0)));
            b=new ChessLogic();Array.Clear(b.Board);b.Turn=ChessColor.Black;
            b.AddPiece(ChessColor.Black,ChessPieceType.King,0,7);
            b.AddPiece(ChessColor.White,ChessPieceType.King,2,5);
            b.AddPiece(ChessColor.White,ChessPieceType.Queen,1,5);
            Assert.IsFalse(b.InCheck(ChessColor.Black));Assert.IsFalse(b.HasLegalMove(ChessColor.Black));
        }

        [TestMethod]
        public void AiMakesOneLegalMoveWithoutCorruptingSearchState()
        {
            var b=new ChessLogic();Move(b,4,1,4,3);
            var before=Snapshot(b);
            ChessPieceCoord from=null,to=null;
            var result=b.AsyncCalculateAiSimpleMove(new ChessAiAsyncTurnKey(),ref from,ref to);
            Assert.IsGreaterThan(0,(int)result);Assert.IsFalse(b.InCheck(ChessColor.Black));
            Assert.AreEqual(ChessColor.White,b.Turn);Assert.AreEqual(2,b.History.Count);
            Assert.AreEqual(ChessColor.Black,b.GetPiece(to).Color);
            b.UndoMove(1);Assert.AreEqual(before,Snapshot(b));
        }

        [TestMethod]
        public void RatingsGainLoseAndRemainMarkedForPersistence()
        {
            // In-memory offline players exercise actual AdjustPlayerRanks/SetProperty,
            // without initializing a shard database or writing anyone's character.
            var players = (Dictionary<uint, OfflinePlayer>)typeof(PlayerManager)
                .GetField("offlinePlayers", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
            var a = Fixture(0x5FFFEE01);
            var b = Fixture(0x5FFFEE02);
            Assert.IsFalse(players.ContainsKey(a.Guid.Full)); Assert.IsFalse(players.ContainsKey(b.Guid.Full));
            players.Add(a.Guid.Full,a); players.Add(b.Guid.Full,b);
            try
            {
                ChessMatch.AdjustPlayerRanks(a.Guid,b.Guid,a.Guid);
                Assert.AreEqual(1425,a.GetProperty(PropertyInt.ChessRank));
                Assert.AreEqual(1375,b.GetProperty(PropertyInt.ChessRank));
                Assert.IsTrue(a.ChangesDetected); Assert.IsTrue(b.ChangesDetected);
                a.ChangesDetected=b.ChangesDetected=false;
                ChessMatch.AdjustPlayerRanks(a.Guid,b.Guid,b.Guid);
                Assert.AreEqual(1396,a.GetProperty(PropertyInt.ChessRank));
                Assert.AreEqual(1404,b.GetProperty(PropertyInt.ChessRank));
                Assert.IsTrue(a.ChangesDetected); Assert.IsTrue(b.ChangesDetected);
                // Restored biota retains the rating instead of returning to the default.
                Assert.AreEqual(1396,a.Biota.PropertiesInt[PropertyInt.ChessRank]);
                var before=a.GetProperty(PropertyInt.ChessRank).Value;
                ChessMatch.AdjustPlayerRanks(a.Guid,new ObjectGuid(0),a.Guid);
                Assert.IsGreaterThan(before,a.GetProperty(PropertyInt.ChessRank).Value);
                // Match completion persists counters as well as ratings. World objects are
                // omitted from this isolated fixture; live rendering is a separate check.
                ChessMatch Match()
                {
                    var game=(Game)RuntimeHelpers.GetUninitializedObject(typeof(Game));
                    var match=new ChessMatch(game){State=ChessState.InProgress};
                    match.Sides[0]=new ChessSide(a.Guid,ChessColor.White);
                    match.Sides[1]=new ChessSide(b.Guid,ChessColor.Black);
                    Array.Clear(match.Logic.Board);game.ChessMatch=match;
                    return match;
                }
                var m=Match();m.Finish(0);
                Assert.AreEqual(1,a.GetProperty(PropertyInt.ChessGamesWon));
                Assert.AreEqual(1,b.GetProperty(PropertyInt.ChessGamesLost));
                Assert.IsNull(m.ChessBoard.ChessMatch);Assert.AreEqual(ChessState.Finished,m.State);
                m=Match();m.QuitDelayed(ChessColor.White);
                Assert.AreEqual(1,a.GetProperty(PropertyInt.ChessGamesLost));
                Assert.AreEqual(1,b.GetProperty(PropertyInt.ChessGamesWon));
                var drawRank=a.GetProperty(PropertyInt.ChessRank);
                m=Match();m.Sides[0].Stalemate=true;
                m.StalemateDelayed(new ChessDelayedAction(ChessDelayedActionType.Stalemate,ChessColor.Black,true));
                Assert.AreEqual(drawRank,a.GetProperty(PropertyInt.ChessRank));
                Assert.AreEqual(3,a.GetProperty(PropertyInt.ChessTotalGames));
                m=Match();m.State=ChessState.WaitingForPlayers;m.QuitDelayed(ChessColor.White);
                Assert.AreEqual(3,a.GetProperty(PropertyInt.ChessTotalGames));
            }
            finally
            {
                players.Remove(a.Guid.Full); players.Remove(b.Guid.Full);
                a.BiotaDatabaseLock.Dispose(); b.BiotaDatabaseLock.Dispose();
            }
        }

        private static OfflinePlayer Fixture(uint id)
        {
            var p=(OfflinePlayer)RuntimeHelpers.GetUninitializedObject(typeof(OfflinePlayer));
            const BindingFlags flags=BindingFlags.NonPublic|BindingFlags.Instance;
            typeof(OfflinePlayer).GetField("<Biota>k__BackingField",flags).SetValue(p,new Biota{Id=id});
            typeof(OfflinePlayer).GetField("<Guid>k__BackingField",flags).SetValue(p,new ObjectGuid(id));
            typeof(OfflinePlayer).GetField("BiotaDatabaseLock").SetValue(p,new ReaderWriterLockSlim());
            return p;
        }
    }
}
