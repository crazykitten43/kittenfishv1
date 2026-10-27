"""KittenFish v1: single-worker iterative deepening alpha-beta UCI engine."""
import ctypes
import pathlib
import sys
import threading
import time
import chess

ROOT = pathlib.Path(__file__).resolve().parents[1]
MATE = 30000
VALUES = [0, 100, 320, 330, 500, 900, 0]


class NNUE:
    def __init__(self, path):
        self.lib = ctypes.CDLL(str(ROOT / 'build/libkfnnue.so'))
        self.lib.kf_load.argtypes = [ctypes.c_char_p]
        self.lib.kf_load.restype = ctypes.c_void_p
        self.lib.kf_free.argtypes = [ctypes.c_void_p]
        self.lib.kf_push.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_int]
        self.lib.kf_pop.argtypes = [ctypes.c_void_p]
        self.lib.kf_eval.argtypes = [ctypes.c_void_p]
        self.ptr = self.lib.kf_load(str(path).encode())
        if not self.ptr:
            raise ValueError('cannot load KFQ1 model: ' + str(path))

    def push(self, board):
        squares = (ctypes.c_int * 64)()
        for sq, piece in board.piece_map().items():
            squares[sq] = piece.piece_type + (0 if piece.color else 6)
        if not self.lib.kf_push(self.ptr, squares, int(not board.turn)):
            raise RuntimeError('NNUE accumulator update failed')

    def pop(self):
        self.lib.kf_pop(self.ptr)

    def eval(self):
        return max(-28000, min(28000, self.lib.kf_eval(self.ptr)))

    def close(self):
        if self.ptr:
            self.lib.kf_free(self.ptr)
            self.ptr = None


class Stopped(Exception):
    pass


class Search:
    def __init__(self, board, stop, net=None, deadline=float('inf'), nodes=None):
        self.board, self.stop, self.net = board, stop, net
        self.deadline, self.limit = deadline, nodes
        self.nodes = 0

    def tick(self):
        if self.stop.is_set() or time.monotonic() >= self.deadline or (self.limit is not None and self.nodes >= self.limit):
            raise Stopped
        self.nodes += 1

    def evaluate(self):
        if self.net:
            return self.net.eval()
        score = sum(VALUES[p.piece_type] * (1 if p.color == self.board.turn else -1)
                    for p in self.board.piece_map().values())
        return score

    def terminal(self, ply):
        if self.board.is_checkmate():
            return -MATE + ply
        if (self.board.is_stalemate() or self.board.is_insufficient_material()
                or self.board.is_fifty_moves() or self.board.is_repetition(3)):
            return 0
        return None

    def ordered(self, moves, first=None):
        def value(m):
            victim = self.board.piece_type_at(m.to_square) or (chess.PAWN if self.board.is_en_passant(m) else 0)
            attacker = self.board.piece_type_at(m.from_square) or 0
            return (m == first, 10 * VALUES[victim] - VALUES[attacker] if victim else -1000,
                    VALUES[m.promotion or 0])
        return sorted(moves, key=value, reverse=True)

    def child(self, move, depth, alpha, beta, ply):
        self.board.push(move)
        pushed = False
        try:
            if self.net:
                self.net.push(self.board)
                pushed = True
            score, pv = self.negamax(depth, -beta, -alpha, ply)
            return -score, pv
        finally:
            if pushed:
                self.net.pop()
            self.board.pop()

    def negamax(self, depth, alpha, beta, ply=0):
        self.tick()
        terminal = self.terminal(ply)
        if terminal is not None:
            return terminal, []
        check = self.board.is_check()
        # A hard safety bound limits checking/capture sequences.
        if ply >= 96:
            return self.evaluate(), []
        if depth <= 0 and not check:
            stand = self.evaluate()
            if stand >= beta:
                return stand, []
            alpha = max(alpha, stand)
            moves = [m for m in self.board.legal_moves if self.board.is_capture(m) or m.promotion]
            best = stand
        else:
            moves = list(self.board.legal_moves)
            best = -MATE - 1
        pv = []
        for move in self.ordered(moves):
            score, tail = self.child(move, depth - 1, alpha, beta, ply + 1)
            if score > best:
                best, pv = score, [move] + tail
            alpha = max(alpha, score)
            if alpha >= beta:
                break
        return best, pv

    def run(self, max_depth=64, roots=None, emit=lambda line: None):
        moves = list(self.board.legal_moves) if roots is None else roots
        if not moves:
            return None
        best = moves[0]
        started = time.monotonic()
        if self.net:
            self.net.push(self.board)
        try:
            for depth in range(1, max_depth + 1):
                score, pv = -MATE - 1, []
                try:
                    for move in self.ordered(moves, best):
                        value, tail = self.child(move, depth - 1, score, MATE + 1, 1)
                        if value > score:
                            score, pv = value, [move] + tail
                except Stopped:
                    break
                best = pv[0]
                ms = max(1, int((time.monotonic() - started) * 1000))
                score_text = ('mate ' + str((MATE - abs(score) + 1) // 2 * (1 if score > 0 else -1))
                              if abs(score) > 29000 else 'cp ' + str(score))
                emit(f'info depth {depth} score {score_text} nodes {self.nodes} time {ms} nps {self.nodes * 1000 // ms} pv ' + ' '.join(m.uci() for m in pv))
                if abs(score) > 29000:
                    break
        finally:
            if self.net:
                self.net.pop()
        return best


def output(line):
    print(line, flush=True)


class UCI:
    def __init__(self):
        self.board = chess.Board()
        self.worker = None
        self.stop = threading.Event()
        self.search = None
        self.overhead = 100
        self.evalfile = ROOT / 'networks/lichess500k.nnue'
        self.net = None
        self.load_net(self.evalfile)

    def load_net(self, path):
        try:
            new = NNUE(path)
        except (OSError, ValueError) as error:
            output('info string ' + str(error) + '; keeping current evaluator')
            return
        if self.net:
            self.net.close()
        self.net, self.evalfile = new, pathlib.Path(path)

    def halt(self):
        self.stop.set()
        if self.worker:
            self.worker.join()
            self.worker = None

    def go(self, args):
        self.halt()
        params, roots = {}, None
        keys = {'depth', 'nodes', 'movetime', 'wtime', 'btime', 'winc', 'binc', 'movestogo', 'mate'}
        i = 0
        while i < len(args):
            key = args[i]
            if key in keys:
                params[key] = max(0, int(args[i + 1]))
                i += 2
            elif key == 'searchmoves':
                roots = []
                i += 1
                while i < len(args) and args[i] not in keys | {'infinite', 'ponder'}:
                    move = self.board.parse_uci(args[i])
                    if move not in roots:
                        roots.append(move)
                    i += 1
            else:
                params[key] = True
                i += 1
        budget = None
        if 'movetime' in params:
            budget = max(1, params['movetime'] - self.overhead)
        elif ('wtime' if self.board.turn else 'btime') in params:
            left = params['wtime' if self.board.turn else 'btime']
            inc = params.get('winc' if self.board.turn else 'binc', 0)
            available = max(1, left - self.overhead)
            budget = min(available, max(1, available / max(1, params.get('movestogo', 30)) + inc * .8))
        self.stop = threading.Event()
        deadline = time.monotonic() + budget / 1000 if budget is not None and not params.get('infinite') and not params.get('ponder') else float('inf')
        search = Search(self.board.copy(stack=True), self.stop, self.net, deadline, params.get('nodes'))
        self.search = search
        self.ponder_budget = budget or 1000
        def work():
            try:
                move = search.run(max(1, min(96, params.get('depth', params.get('mate', 32) * 2))), roots, output)
                # Infinite/ponder searches must wait for stop/ponderhit even if mate is found.
                if params.get('infinite') or params.get('ponder'):
                    while not self.stop.wait(.01) and time.monotonic() < search.deadline:
                        pass
                output('bestmove ' + (move.uci() if move else '0000'))
            except Exception as error:
                output('info string search failed: ' + str(error))
                legal = roots if roots is not None else list(search.board.legal_moves)
                output('bestmove ' + (legal[0].uci() if legal else '0000'))
        self.worker = threading.Thread(target=work)
        self.worker.start()

    def command(self, line):
        args = line.split()
        if not args:
            return True
        cmd, rest = args[0], args[1:]
        if cmd == 'uci':
            output('id name KittenFish v1')
            output('id author crazykitten43')
            output('option name Move Overhead type spin default 100 min 0 max 5000')
            output('option name EvalFile type string default ' + str(self.evalfile))
            output('uciok')
        elif cmd == 'isready':
            output('readyok')
        elif cmd == 'stop':
            self.halt()
        elif cmd == 'ponderhit' and self.search:
            self.search.deadline = time.monotonic() + self.ponder_budget / 1000
        elif cmd == 'ucinewgame':
            self.halt()
            self.board = chess.Board()
        elif cmd == 'position':
            self.halt()
            split = rest.index('moves') if 'moves' in rest else len(rest)
            board = chess.Board() if rest[0] == 'startpos' else chess.Board(' '.join(rest[1:split]))
            if not board.is_valid():
                raise ValueError('invalid chess position')
            for move in rest[split + 1:]:
                board.push_uci(move)
            self.board = board
        elif cmd == 'go':
            self.go(rest)
        elif cmd == 'setoption':
            self.halt()
            at = rest.index('value')
            name, value = ' '.join(rest[1:at]), ' '.join(rest[at + 1:])
            if name == 'Move Overhead':
                self.overhead = max(0, min(5000, int(value)))
            elif name == 'EvalFile':
                self.load_net(value)
        elif cmd == 'quit':
            self.halt()
            return False
        return True

    def run(self):
        try:
            for line in sys.stdin:
                try:
                    if not self.command(line):
                        break
                except (ValueError, IndexError) as error:
                    output('info string invalid command: ' + str(error))
        finally:
            self.halt()
            if self.net:
                self.net.close()


if __name__ == '__main__':
    UCI().run()
